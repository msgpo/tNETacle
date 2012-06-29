/**
 * Copyright (c) 2012, PICHOT Fabien Paul Leonard <pichot.fabien@gmail.com>
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
**/


#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#if defined Unix
# include <unistd.h>
#endif

#include "networking.h"

#include "tnetacle.h"
#include "options.h"
#include "mc.h"
#include "tntsocket.h"
#include "server.h"
#include "log.h"
#include "hexdump.h"

extern struct options serv_opts;

union chartoshort {
    unsigned char *cptr;
    unsigned short *sptr;
}; /* The goal of this union is to properly convert uchar* to ushort* */


static void
server_mc_read_cb(struct bufferevent *bev, void *ctx)
{
    struct server *s = (struct server *)ctx;
    ssize_t n;
    struct evbuffer *buf = NULL;
    struct mc *it = NULL;
    struct mc *ite = NULL;
    unsigned short size;
    intptr_t device_fd;

#if defined Windows
    device_fd = s->devide_fd;
#else
    device_fd = event_get_fd(s->device);
#endif
    buf = bufferevent_get_input(bev);
    while (evbuffer_get_length(buf) != 0)
    {
        /*
         * Using chartoshort union to explicitly convert form uchar* to ushort*
         * without warings.
         */
        union chartoshort u;
        u.cptr = evbuffer_pullup(buf, sizeof(size));
        size = ntohs(*u.sptr);
        if (size > evbuffer_get_length(buf))
        {
            log_debug("receive an incomplete frame of %d(%-#2x) bytes but "
                      "only %d bytes are available", size, *(u.sptr),
                      evbuffer_get_length(buf));
            break;
        }
        log_debug("receive a frame of %d(%-#2x) bytes", size, *(u.sptr));
        for (it = v_mc_begin(&s->peers),
             ite = v_mc_end(&s->peers);
             it != ite;
             it = v_mc_next(it))
        {
            int err;

            if (it->bev == bev)
                continue;
            err = bufferevent_write(it->bev, evbuffer_pullup(buf, sizeof(size) + size), sizeof(size) + size);
            if (err == -1)
            {
                log_notice("error while crafting the buffer to send to %p", it);
                break;
            }
            log_debug("adding %d(%-#2x) bytes to %p's output buffer",
                      size, size, it);
        }
        evbuffer_drain(buf, sizeof(size));
        n = write(device_fd, evbuffer_pullup(buf, size), size);
        evbuffer_drain(buf, size);
    }
}

static int
_server_match_bev(struct mc const *a, struct mc const *b)
{
    return a->bev == b->bev;
}

static void
server_mc_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    struct server *s = (struct server *)ctx;

    (void)bev;
    (void)s;
    if (events & BEV_EVENT_CONNECTED)
    {
        struct mc *mc;
        struct mc tmp;

        tmp.bev = bev;
        mc = v_mc_find_if(&s->pending_peers, &tmp, _server_match_bev);
        if (mc != v_mc_end(&s->pending_peers))
        {
            log_notice("standard connexion established.");
            memcpy(&tmp, mc, sizeof(tmp));
            v_mc_erase(&s->pending_peers, mc);
            v_mc_push(&s->peers, &tmp);
        }
    }
    if (events & BEV_EVENT_ERROR)
    {
        struct mc *mc;
        struct mc tmp;
        int everr;
        int sslerr;

        tmp.bev = bev;
        everr = EVUTIL_SOCKET_ERROR();

        if (everr != 0)
        {
            log_notice("closing the socket with error closing the meta-connexion: (%d) %s",
                       everr, evutil_socket_error_to_string(everr));
        }
        while ((sslerr = bufferevent_get_openssl_error(bev)) != 0)
        {
            log_notice("SSL error code (%d): %s in %s %s",
                       sslerr, ERR_reason_error_string(sslerr),
                       ERR_lib_error_string(sslerr),
                       ERR_func_error_string(sslerr));
        }
        mc = v_mc_find_if(&s->pending_peers, &tmp, _server_match_bev);
        if (mc != v_mc_end(&s->pending_peers))
        {
            mc_close(mc);
            v_mc_erase(&s->pending_peers, mc);
            log_debug("socket removed from the pending list");
        }
        else
        {
            mc = v_mc_find_if(&s->peers, &tmp, _server_match_bev);
            if (mc != v_mc_end(&s->peers))
            {
                mc_close(mc);
                v_mc_erase(&s->peers, mc);
                log_debug("socket removed from the peer list");
            }
        }
    }
}

static void
listen_callback(struct evconnlistener *evl, evutil_socket_t fd,
                struct sockaddr *sock, int len, void *ctx)
{
    struct server *s = (struct server *)ctx;
    struct event_base *base = evconnlistener_get_base(evl);
    int errcode;
    struct mc mc;

    memset(&mc, 0, sizeof mc);
    mc.ssl_flags = BUFFEREVENT_SSL_ACCEPTING;
    errcode = mc_init(&mc, base, fd, sock, (socklen_t)len, s->server_ctx);
    if (errcode != -1)
    {
        bufferevent_setcb(mc.bev, server_mc_read_cb, NULL,
                          server_mc_event_cb, s);
        log_debug("add the ssl client to the list");
        v_mc_push(&s->peers, &mc);
    }
    else
    {
        log_notice("Failed to init a meta connexion");
    }
}

static void
accept_error_cb(struct evconnlistener *evl, void *ctx)
{
    (void)evl;
    (void)ctx;
}

static void
broadcast_to_peers(struct server *s)
{
    struct frame *fit = v_frame_begin(&s->frames_to_send);
    struct frame *fite = v_frame_end(&s->frames_to_send);
    struct mc *it = NULL;
    struct mc *ite = NULL;

    for (;fit != fite; fit = v_frame_next(fit))
    {
        unsigned short size_networked = htons(fit->size);
        for (it = v_mc_begin(&s->peers),
             ite = v_mc_end(&s->peers);
             it != ite;
             it = v_mc_next(it))
        {
            int err;

            err = bufferevent_write(it->bev, &size_networked, sizeof(size_networked));
            if (err == -1)
            {
                log_notice("error while crafting the buffer to send to %p", it);
                break;
            }
            err = bufferevent_write(it->bev, fit->frame, fit->size);
            if (err == -1)
            {
                log_notice("error while crafting the buffer to send to %p", it);
                break;
            }
            log_debug("adding %d(%-#2x) bytes to %p's output buffer",
                      fit->size, fit->size, it);
        }
    }
    v_frame_erase_range(&s->frames_to_send, v_frame_begin(&s->frames_to_send), fite);
}

static void
server_device_cb(evutil_socket_t device_fd, short events, void *ctx)
{
    struct server *s = (struct server *)ctx;

#if defined Windows
    device_fd =	s->devide_fd;
    if (events & EV_TIMEOUT)
#else
    if (events & EV_READ)
#endif
    {
        int _n = 0;
        ssize_t n;
        struct frame tmp;

        while ((n = read(device_fd, &tmp.frame, sizeof(tmp.frame))) > 0)
        {
            tmp.size = (unsigned short)n;/* We cannot read more than a ushort*/
            v_frame_push(&s->frames_to_send, &tmp);
            //log_debug("Read a new frame of %d bytes.", n);
            _n++;
        }
#if defined Windows
        if (n == 0 || GetLastError() == ERROR_IO_PENDING)
#else
        if (n == 0 || EVUTIL_SOCKET_ERROR() == EAGAIN) /* no errors occurs*/
#endif
        {
            //log_debug("Read %d frames in this pass.", _n);
            broadcast_to_peers(s);
        }
        else if (n == -1)
            log_warn("read on the device failed:");
    }
}

#if defined Windows
void
server_set_device(struct server *s, int fd)
{
    struct evconnlistener **it = NULL;
    struct evconnlistener **ite = NULL;
    struct event_base *evbase = s->evbase;
	struct event *ev = event_new(evbase, -1, EV_PERSIST, server_device_cb, s);
    int err;

    /*Sadly, this don't work on windows :(*/
    s->devide_fd = fd;
    s->tv.tv_sec = 0;
    s->tv.tv_usec = 5000;
    if (ev == NULL)
    {
        log_warn("failed to allocate the event handler for the device:");
        return;
    }
    s->device = ev;
    event_add(s->device, &s->tv);
    log_notice("event handler for the device sucessfully configured");
    it = v_evl_begin(&s->srv_list);
    ite = v_evl_end(&s->srv_list);
    for (; it != ite; it = v_evl_next(it))
    {
        evconnlistener_enable(*it);
    }
    log_notice("listener started");
}
#else

void
server_set_device(struct server *s, int fd)
{
    struct evconnlistener **it = NULL;
    struct evconnlistener **ite = NULL;
    struct event_base *evbase = s->evbase;
    struct event *ev = event_new(evbase, fd, EV_READ | EV_PERSIST,
                                 server_device_cb, s);
    int err;

    err = evutil_make_socket_nonblocking(fd);
    if (err == -1)
        log_debug("fuck !");

    if (ev == NULL)
    {
        log_warn("failed to allocate the event handler for the device:");
        return;
    }
    s->device = ev;
    event_add(s->device, NULL);
    log_notice("event handler for the device sucessfully configured");

    it = v_evl_begin(&s->srv_list);
    ite = v_evl_end(&s->srv_list);
    for (; it != ite; it = v_evl_next(it))
    {
        evconnlistener_enable(*it);
    }
    log_notice("listener started");
}
#endif

static SSL_CTX *
evssl_init(void)
{
    SSL_CTX  *server_ctx;

    /* Initialize the OpenSSL library */
    SSL_load_error_strings();
    SSL_library_init();
    /* We MUST have entropy, or else there's no point to crypto. */
    if (!RAND_poll())
        return NULL;

    server_ctx = SSL_CTX_new(SSLv23_method());

    if (! SSL_CTX_use_certificate_chain_file(server_ctx, "cert") ||
        ! SSL_CTX_use_PrivateKey_file(server_ctx, "pkey", SSL_FILETYPE_PEM)) {
        puts("Couldn't read 'pkey' or 'cert' file.  To generate a key\n"
           "and self-signed certificate, run:\n"
           "  openssl genrsa -out pkey 2048\n"
           "  openssl req -new -key pkey -out cert.req\n"
           "  openssl x509 -req -days 365 -in cert.req -signkey pkey -out cert");
        return NULL;
    }
    return server_ctx;
}

int
server_init(struct server *s, struct event_base *evbase)
{
    struct cfg_sockaddress *it_listen = NULL;
    struct cfg_sockaddress *ite_listen = NULL;
    struct cfg_sockaddress *it_peer = NULL;
    struct cfg_sockaddress *ite_peer = NULL;
    int err;
    size_t i = 0;

#if defined Windows
	gl_overlap.hEvent = CreateEvent(NULL, /*Auto reset*/FALSE,
		                                  /*Initial state*/FALSE,
										  TEXT("Device event"));
#endif

    it_listen = v_sockaddr_begin(&serv_opts.listen_addrs);
    ite_listen = v_sockaddr_end(&serv_opts.listen_addrs);
    it_peer = v_sockaddr_begin(&serv_opts.peer_addrs);
    ite_peer = v_sockaddr_end(&serv_opts.peer_addrs);

    v_mc_init(&s->peers);
    v_mc_init(&s->pending_peers);
    v_frame_init(&s->frames_to_send);
    v_evl_init(&s->srv_list);
    s->evbase = evbase;

    if (serv_opts.encryption)
        s->server_ctx = evssl_init();
    else
        s->server_ctx = NULL;

    /*evbase = evconnlistener_get_base(s->srv);*/

    /* Listen on all ListenAddress */
    for (; it_listen != ite_listen; it_listen = v_sockaddr_next(it_listen), ++i)
    {
        struct evconnlistener *evl = NULL;

        evl = evconnlistener_new_bind(evbase, listen_callback,
          s, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
          (struct sockaddr *)&it_listen->sockaddr, it_listen->len);
        if (evl == NULL) {
            log_debug("fail at ListenAddress #%i", i);
             continue;
        }
        evconnlistener_set_error_cb(evl, accept_error_cb);
        evconnlistener_disable(evl);
        v_evl_push(&s->srv_list, evl);
    }

    /* If we don't have any PeerAddress it's finished */
    if (serv_opts.peer_addrs.size == 0)
        return 0;

    for (;it_peer != ite_peer; it_peer = v_sockaddr_next(it_peer))
    {
        struct mc mc;

        memset(&mc, 0, sizeof mc);
        mc.ssl_flags = BUFFEREVENT_SSL_CONNECTING;
        err = mc_init(&mc, evbase, -1, (struct sockaddr *)&it_peer->sockaddr,
                      it_peer->len, s->server_ctx);
        if (err == -1) {
            log_warn("unable to allocate a socket for connecting to the peer");
            return -1;
        }
        bufferevent_setcb(mc.bev, server_mc_read_cb, NULL, server_mc_event_cb, s);
        err = bufferevent_socket_connect(mc.bev,
                                         (struct sockaddr *)&it_peer->sockaddr,
                                         it_peer->len);
        if (err == -1) {
            log_warn("unable to initiate a connexion to the peer");
            return -1;
        }
        v_mc_push(&s->pending_peers, &mc);
    }
    return 0;
}
