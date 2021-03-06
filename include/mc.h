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

#ifndef MC_ENDPOINT_JU2N66SJ
#define MC_ENDPOINT_JU2N66SJ

#include "networking.h"

/*
 * We include this one after networking.h because openssl includes windows.h
 * leading to a redifinition of most of the wsaapi symbols on Windows.
 * Seriously, fuck you OpenSSL.
 */
#include <openssl/ssl.h> /*Can not forward declare SSL types..*/
#include "endpoint.h"

struct bufferevent;
struct event_base;
struct sockaddr;
struct server;
struct frame;
struct udp;

enum tls_flags
{
    TLS_ENABLE = (1 << 0),
    TLS_DISABLE = (1 << 1),
};

struct mc
{
    struct peer {
        struct sockaddr     *address;
        socklen_t           len;
    } p;
    struct bufferevent  *bev;
    int                 ssl_flags;
    int                 tunel;
};

int mc_init(struct mc *,
            struct event_base *,
            evutil_socket_t fd,
            struct sockaddr *,
            socklen_t len,
            SSL_CTX *server_ctx);

void mc_close(struct mc *);

size_t mc_add_raw_data(struct mc *,
                       void *,
                       size_t);

int mc_hello(struct mc *,
             struct udp *);

int mc_establish_tunnel(struct mc *,
                        struct udp *);

struct mc *mc_peer_accept(struct server *s,
                          struct event_base *evbase,
                          struct sockaddr *sock,
                          int socklen,
                          evutil_socket_t fd);

struct mc *mc_peer_connect(struct server *s,
                           struct event_base *evbase,
                           struct sockaddr *sock,
                           int socklen);

int mc_established(struct server *s,
                   struct sockaddr *,
                   int socklen);

int mc_pending(struct server *s,
                   struct sockaddr *,
                   int socklen);

/* used for debug, and print a mc */
char *mc_presentation(struct mc*,
                      char *name,
                      int size);

char *address_presentation(struct sockaddr *,
                           int socklen,
                           char *, int);

#endif /* end of include guard: MC_ENDPOINT_JU2N66SJ */


