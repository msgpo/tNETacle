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

#include <sys/types.h>
#if !defined WIN32
#include <sys/socket.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined Windows
# include <WS2tcpip.h>
# include <io.h>
# include <stdio.h>
# define write _write
# define ssize_t SSIZE_T
#endif

#if defined Unix
# include <unistd.h>
# include <netinet/in.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <errno.h>
#include <string.h>
#if !defined Windows
#include <unistd.h>
#else
#include <io.h>
#include <WS2tcpip.h>
#define ssize_t SSIZE_T
#endif

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

#if defined Windows
static void
send_buffer_to_device_thread(struct evbuffer *buf, size_t size, struct server *s)
{
    struct evbuffer *output = bufferevent_get_output(s->pipe_endpoint);

    evbuffer_add(output, evbuffer_pullup(buf, size), size);
}
#endif

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
#if defined Windows
        send_buffer_to_device_thread(buf, sizeof(size) + size, s);
        evbuffer_drain(buf, sizeof(size) + size);
#else
        evbuffer_drain(buf, sizeof(size));
		n = write(event_get_fd(s->device), evbuffer_pullup(buf, size), size);
        evbuffer_drain(buf, size);
#endif
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
        log_debug("moving the state from pending to connected");
        mc = v_mc_find_if(&s->pending_peers, &tmp, _server_match_bev);
        if (mc != v_mc_end(&s->pending_peers))
        {
            memcpy(&tmp, mc, sizeof(tmp));
            v_mc_erase(&s->pending_peers, mc);
            v_mc_push(&s->peers, &tmp);
            log_debug("state moved");
        }
    }
    if (events & BEV_EVENT_ERROR)
    {
        struct mc *mc;
        struct mc tmp;
        int errcode;

        errcode = evutil_socket_geterror(bufferevent_getfd(bev));
        log_notice("%s", evutil_socket_error_to_string(errcode));
        tmp.bev = bev;
        log_notice("the socket closed with error closing the meta-connexion");

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
    struct bufferevent *bev = bufferevent_socket_new(base, fd,
                                                     BEV_OPT_CLOSE_ON_FREE);
    log_debug("new connection");
    if (bev != NULL)
    {
        struct mc mctx;

        /*
         * Set a callback to bev using 
         * bufferevent_setcb.
         */
        mc_init(&mctx, sock, (socklen_t)len, bev);
        bufferevent_setcb(bev, server_mc_read_cb, NULL, server_mc_event_cb, s);
        v_mc_push(&s->peers, &mctx);
    }
    else
    {
        log_debug("failed to allocate bufferevent");
    }
}

static void
accept_error_cb(struct evconnlistener *evl, void *ctx)
{
    (void)evl;
    (void)ctx;
}

static char *
peer_name(struct mc *mc, char *name, int len)
{
    struct sockaddr *sock = mc->p.address;

    if (sock->sa_family == AF_INET)
    {
        struct sockaddr_in *sin = (struct sockaddr_sin *)sock;
        char tmp[64];

        evutil_inet_ntop(AF_INET, &sin->sin_addr, tmp, sizeof tmp);
        _snprintf(name, len, "%s:%d", tmp, ntohs(sin->sin_port));
        return name;
    }
    else
    {
        struct sockaddr_in6 *sin6 = (struct sockaddr_sin6 *)sock;
        char tmp[128];

        evutil_inet_ntop(AF_INET6, &sin6->sin6_addr, tmp, sizeof tmp);
        _snprintf(name, len, "%s:%d", tmp, ntohs(sin6->sin6_port));
        return name;
    }
}

#if defined Windows
/*static */void
#else
static void
#endif
broadcast_to_peers(struct server *s)
{
    struct frame *fit = v_frame_begin(&s->frames_to_send);
    struct frame *fite = v_frame_end(&s->frames_to_send);
	struct mc *it = NULL;
	struct mc *ite = NULL;
    char name[512];

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
                log_notice("error while crafting the buffer to send to %s", peer_name(it, name, sizeof name));
                break;
            }
            err = bufferevent_write(it->bev, fit->frame, fit->size);
            if (err == -1)
            {
                log_notice("error while crafting the buffer to send to %s", peer_name(it, name, sizeof name));
                break;
            }
            log_debug("adding %d(%-#2x) bytes to %s's output buffer",
                      fit->size, fit->size, peer_name(it, name, sizeof name));
        }
    }
    v_frame_erase_range(&s->frames_to_send, v_frame_begin(&s->frames_to_send), fite);
}

static void
server_device_cb(evutil_socket_t device_fd, short events, void *ctx)
{
    struct server *s = (struct server *)ctx;

    if (events & EV_READ)
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
        if (n == 0 || EVUTIL_SOCKET_ERROR() == EAGAIN) /* no errors occurs*/
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
    log_notice("event handler for the device sucessfully configured");
    evconnlistener_enable(s->srv);
    log_notice("listener started");
}
#else

void
server_set_device(struct server *s, int fd)
{
    struct event_base *evbase = evconnlistener_get_base(s->srv);
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
    evconnlistener_enable(s->srv);
    log_notice("listener started");
}
#endif

int
server_init(struct server *s, struct event_base *evbase)
{
    struct evconnlistener *evl;
    struct bufferevent *bev;
    struct mc mctx;
    struct sockaddr *listens;
    struct sockaddr *peers;
    int err;
    size_t i;

    peers = v_sockaddr_begin(&serv_opts.peer_addrs);

    v_mc_init(&s->peers);
    v_mc_init(&s->pending_peers);
    v_frame_init(&s->frames_to_send);

    /*evbase = evconnlistener_get_base(s->srv);*/

    /* Listen on all ListenAddress */
    for (listens = v_sockaddr_begin(&serv_opts.listen_addrs);
         listens != NULL && listens != v_sockaddr_end(&serv_opts.listen_addrs);
         listens = v_sockaddr_next(listens)) {
        evl = evconnlistener_new_bind(evbase, listen_callback,
          s, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
          listens, sizeof(*listens));
        if (evl == NULL) {
            log_debug("fail at ListenAddress #%i", i);
            return -1;
        }
    }
    evconnlistener_set_error_cb(evl, accept_error_cb);
    evconnlistener_disable(evl);
    s->srv = evl;

    /* If we don't have any PeerAddress it's finished */
    if (serv_opts.peer_addrs.size == 0)
        return 0;

    for (;peers != NULL && peers != v_sockaddr_end(&serv_opts.peer_addrs);
         peers = v_sockaddr_next(peers)) {
        bev = bufferevent_socket_new(evbase, -1, BEV_OPT_CLOSE_ON_FREE);
        if (bev == NULL) {
            log_warn("unable to allocate a socket for connecting to the peer");
            break;
        }
        err = bufferevent_socket_connect(bev, peers, sizeof(*peers));
        if (err == -1) {
            log_warn("unable to connect to the peer");
            break;
        }
        mc_init(&mctx, peers, sizeof(*peers), bev);
        bufferevent_setcb(bev, server_mc_read_cb, NULL, server_mc_event_cb, s);
        v_mc_push(&s->pending_peers, &mctx);
    }

    return 0;
}
