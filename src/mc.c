/**
 * Copyright (c) 2012, PICHOT Fabien Paul Leonard <pichot.fabien@gmail.com>
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
**/

#include <stdlib.h>
#include <string.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <event2/util.h>

#include <openssl/err.h>

#include "networking.h"
#include "mc.h"
#include "log.h"
#include "server.h"
#include "tnetacle.h"
#include "options.h"

extern struct options serv_opts;

/*
 * Returns a human readable presentation of a sockaddr
 * Fills the char *name parameter, and return its address.
 */

char *
address_presentation(struct sockaddr *sock, int socklen,
                     char *name, int namelen)
{
    (void)socklen;
    if (sock->sa_family == AF_INET)
    {
        struct sockaddr_in *sin = (struct sockaddr_in *)sock;
        char tmp[64];

        evutil_inet_ntop(AF_INET, &sin->sin_addr, tmp, sizeof tmp);
        evutil_snprintf(name, namelen, "%s:%d", tmp, ntohs(sin->sin_port));
        return name;
    }
    else
    {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sock;
        char tmp[128];

        evutil_inet_ntop(AF_INET6, &sin6->sin6_addr, tmp, sizeof tmp);
        evutil_snprintf(name, namelen, "%s:%d", tmp, ntohs(sin6->sin6_port));
        return name;
    }
}

/*
 * Returns a human readable presentation of the mc endpoint.
 * Fills the char *name parameter, and return its address.
 */
char *
mc_presentation(struct mc *self, char *name, int len)
{
    struct sockaddr *sock = self->p.address;
    int socklen = self->p.len;

    return address_presentation(sock, socklen, name, len);
}

/*
 * This function init a new struct mc.
 * struct event_base *evb: a pointer to a correct event_base
 * int fd: optional, an fd already opened to create the underlying event layer.
 *     -1 for creating the socket here.
 * struct sockaddr *s, socklent_t len: pointer to the coresponding sockaddr
 *     and its len. It will be copied inside.
 * SSL_CTX* server_ctx: optional, the ssl context of the server.
 *     Used to initialised the underlying bufferevent_ssl. Can be NULL.
 *
 */

int
mc_init(struct mc *self, struct event_base *evb, int fd, struct sockaddr *s,
        socklen_t len, SSL_CTX *server_ctx)
{
    struct sockaddr *tmp = malloc(len);

    if (server_ctx != NULL)
    {
        SSL *client_ctx = SSL_new(server_ctx);
        self->bev = bufferevent_openssl_socket_new(evb, fd, client_ctx,
                                                   self->ssl_flags,
                                                   BEV_OPT_CLOSE_ON_FREE);
    }
    else
    {
        self->bev = bufferevent_socket_new(evb, fd, BEV_OPT_CLOSE_ON_FREE);
    }

    
    if (tmp == NULL || self->bev == NULL)
    {
        log_notice("failed to allocate the memory needed to establish a new "
                   "meta-connection");
        return -1;
    }
    memcpy(tmp, s, len);
    self->p.address = tmp;
    self->p.len = len;
    bufferevent_enable(self->bev, EV_READ | EV_WRITE);
    return 0;
}

void
mc_close(struct mc *self)
{
    free(self->p.address);
    bufferevent_free(self->bev);
}

/*
 * This function will add a frame to the output buffer of "self".
 * This function will take care to convert the size to network byte order
 * before adding it.
 * Returns -1 on error, and the frame size otherwise.
 */

int
mc_add_frame(struct mc *self, struct frame *f)
{
    unsigned short size_networked = htons(f->size);
    struct evbuffer *output = bufferevent_get_output(self->bev);
    char name[128];
    int err;

    err = evbuffer_add(output, &size_networked, sizeof(size_networked));
    if (err == -1)
    {
        log_notice("error while crafting the buffer to send to %s", mc_presentation(self, name, sizeof name));
        return -1;
    }
    err = evbuffer_add(output, f->frame, f->size);
    if (err == -1)
    {
        log_notice("error while crafting the buffer to send to %s", mc_presentation(self, name, sizeof name));
        return -1;
    }
    return f->size;
}

/*
 * This function will add raw data to the output buffer.
 * As you might guess, this function can not check anything about the input data,
 * so try not to use it to much.
 * The return value is -1 if the function failed, the number of bytes added otherwise
 */

int
mc_add_raw_data(struct mc *self, void *data, size_t size)
{
    struct evbuffer *output = bufferevent_get_output(self->bev);
    char name[128];
    int err;

    err = evbuffer_add(output, data, size);
    if (err == -1)
    {
        log_notice("error while add %d raw data into %p's output buffer",
            size, mc_presentation(self, name, sizeof name));
    }
    return size;
}
