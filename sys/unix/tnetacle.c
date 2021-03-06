/*
 * Copyright (c) 2011 Tristan Le Guern <leguern AT medu DOT se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>

/* imsg specific includes */
#include <sys/uio.h>
#include <sys/queue.h>
#include <imsg.h>

#include <event2/event.h>
#include <event2/util.h>

#include "tntexits.h"
#include "tnetacle.h"
#include "log.h"
#include "options.h"
#include "mc.h"
#include "server.h"
#include "device.h"

extern struct options serv_opts;

struct imsg_data {
    struct imsgbuf *ibuf;
    struct event_base *evbase;
    struct server *server;
    int is_ready_read;
    int is_ready_write;
};

volatile sig_atomic_t chld_quit;

int tnt_dispatch_imsg(struct imsg_data *);

static void
tnt_imsg_callback(evutil_socket_t fd, short events, void *args) {
    (void)fd;
    struct imsg_data *data = args;
    struct imsgbuf *ibuf = data->ibuf;

    if (events & EV_READ || data->is_ready_read == 1) {
        data->is_ready_read = 1;
        if (tnt_dispatch_imsg(data) == -1)
            data->is_ready_read = 0;
    }
    if (events & EV_WRITE || data->is_ready_write == 1) {
        data->is_ready_write = 1;
        if (ibuf->w.queued > 0) {
            if (msgbuf_write(&ibuf->w) == -1)
                data->is_ready_write = 0;
        }
    }
}

static void
chld_sighdlr(evutil_socket_t sig, short events, void *args) {
    struct event_base *evbase = args;

    char *name = "unknow";
    (void)events;

    switch (sig)
    {
        case SIGTERM:
            name = "sigterm";
            break;
        case SIGINT:
            name = "sigint";
            break;
    }
    log_warnx("received signal %s(%d), stopping", name, sig);
    event_base_loopbreak(evbase);
}

static void
tnt_priv_drop(struct passwd *pw) {
    struct stat ss; /* Heil! */

    /* All this part is a preparation to the privileges dropping */
    if (stat(pw->pw_dir, &ss) == -1)
        log_err(1, "%s", pw->pw_dir);
    if (ss.st_uid != 0) 
        log_errx(1, "_tnetacle's home has unsafe owner");
    if ((ss.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        log_errx(1, "_tnetacle's home has unsafe permissions");
    if (chroot(pw->pw_dir) == -1)
        log_err(1, "%s", pw->pw_dir);
    if (chdir("/") == -1)
        log_err(1, "%s", pw->pw_dir);
    /*
     * TODO:
     * if debug is not set dup stdin, stdout and stderr to /dev/null
     */
    if (setgroups(1, &pw->pw_gid) == -1)
        log_err(1, "can't drop privileges (setgroups)");
#ifdef HAVE_SETRESXID
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
        log_err(1, "can't drop privileges (setresgid)");
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
        log_err(1, "can't drop privileges (setresuid)");
#else
    /* Fallback to setuid, but it might not work */
    if (setgid(pw->pw_gid) == -1)
        log_err(1, "can't drop privileges (setgid)");
    if (setuid(pw->pw_uid) == -1)
        log_err(1, "can't drop privileges (setuid)");
#endif
}

static struct event *
init_pipe_endpoint(int imsg_fds[2], struct imsg_data *data) {
    struct event *event = NULL;

    if (close(imsg_fds[0]))
        log_notice("close");

    data->is_ready_write = 0;
    data->is_ready_read = 0;
    imsg_init(data->ibuf, imsg_fds[1]);
    evutil_make_socket_nonblocking(imsg_fds[1]);
    event = event_new(data->evbase, imsg_fds[1],
                      EV_READ | EV_WRITE | EV_ET | EV_PERSIST,
                      &tnt_imsg_callback, data);
    return event;
}

int
tnt_fork(int imsg_fds[2]) {
    pid_t pid;
    struct imsgbuf ibuf;
    struct imsg_data data;
    struct passwd *pw;
    struct event_base *evbase = NULL;
    struct event *sigterm = NULL;
    struct event *sigint = NULL;
    struct event *imsg_event = NULL;
    struct server server;
    struct event_config *evcfg;

    switch ((pid = fork())) {
        case -1:
            log_err(TNT_OSERR, "fork");
            break;
        case 0:
            tnt_setproctitle("[unpriv]");
            log_set_prefix("unpriv");
            break;
        default:
            tnt_setproctitle("[priv]");
            log_set_prefix("priv");
            return pid;
    }

    if ((pw = getpwnam(TNETACLE_USER)) == NULL) {
        log_errx(1, "unknown user " TNETACLE_USER);
        return TNT_NOUSER;
    }

    /*Allocate the event config*/
    evcfg = event_config_new();

    /* Initialize the OpenSSL library */
    SSL_library_init();
    SSL_load_error_strings();
    /* We MUST have entropy, or else there's no point to crypto. */
    if (!RAND_poll())
    {
        log_errx(TNT_SOFTWARE, "[INIT] failed to find an entropy source");
        /* never returns */
    }

    if (serv_opts.encryption)
        server.server_ctx = evssl_init();
    else
        server.server_ctx = NULL;

    tnt_priv_drop(pw);

#if defined(Darwin)
    /* It's sad isn't it ?*/
    event_config_avoid_method(evcfg, "kqueue");
    event_config_avoid_method(evcfg, "poll");
    event_config_avoid_method(evcfg, "devpoll");
    if ((evbase = event_base_new_with_config(evcfg)) == NULL) {
        log_err(1, "libevent");
    }
#else
    if ((evbase = event_base_new_with_config(evcfg)) == NULL) {
        log_err(1, "libevent");
    }
#endif

    sigterm = event_new(evbase, SIGTERM, EV_SIGNAL, &chld_sighdlr, evbase);
    sigint = event_new(evbase, SIGINT, EV_SIGNAL, &chld_sighdlr, evbase);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    if (server_init(&server, evbase) == -1)
        log_errx(1, "failed to init the server socket");

    data.ibuf = &ibuf;
    data.evbase = evbase;
    data.server = &server;
    imsg_event = init_pipe_endpoint(imsg_fds, &data);

    event_add(sigterm, NULL);
    event_add(sigint, NULL);
    event_add(imsg_event, NULL);

    log_info("tnetacle ready");

    /* Immediately request the creation of a tun interface */
    imsg_compose(&ibuf, IMSG_CREATE_DEV, 0, 0, -1, NULL, 0);

    log_info("starting event loop");
    event_base_dispatch(evbase);

    /* cleanely exit */
    msgbuf_write(&ibuf.w);
    msgbuf_clear(&ibuf.w);

    /* Shutdown the server */
    server_delete(&server);

    /*
     * It may look like we freed this one twice,
     * once here and once in tnetacled.c but this is not the case.
     * Please don't erase this !
     */

    event_free(sigterm);
    event_free(sigint);
    close(event_get_fd(imsg_event));
    event_free(imsg_event);
    event_base_free(evbase);
    event_config_free(evcfg);

    log_info("tnetacle exiting");
    exit(TNT_OK);
}

/*
 * The purpose of this function is to handle requests sent by the
 * root privileged process.
 */
int
tnt_dispatch_imsg(struct imsg_data *data) {
    struct imsg imsg;
    ssize_t n;
    int device_fd;
    struct imsgbuf *ibuf = data->ibuf;

    n = imsg_read(ibuf);
    if (n == -1) {
        log_warnx("loose some imsgs");
        imsg_clear(ibuf);
        return -1;
    }

    if (n == 0) {
        log_warnx("pipe closed");
        return -1;
    }

    /* Loops through the queue created by imsg_read */
    while ((n = imsg_get(ibuf, &imsg)) != 0 && n != -1) {
        switch (imsg.hdr.type) {
            case IMSG_CREATE_DEV:
                device_fd = imsg.fd;
                log_info("receive IMSG_CREATE_DEV: fd %i", device_fd);

                server_set_device(data->server, device_fd);

                /* directly ask to configure the tun device */
                imsg_compose(ibuf, IMSG_SET_IP, 0, 0, -1,
                             serv_opts.addr , strlen(serv_opts.addr));
                break;
            default:
                break;
        }
        imsg_free(&imsg);
    }
    if (n == -1) {
        log_warnx("imsg_get");
        return -1;
    }
    return 0;
}


