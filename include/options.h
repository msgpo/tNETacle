/*
 * Copyright (c) 2012 Tristan Le Guern <leguern AT medu DOT se>
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
#if !defined Windows
# include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#endif /* Windows */

#include "tnetacle.h"
#include "networking.h"

#ifndef TNT_OPTIONS_H_ 
#define TNT_OPTIONS_H_ 

struct cfg_sockaddress {
    int len;
    struct sockaddr_storage sockaddr;
};

#define VECTOR_TYPE struct cfg_sockaddress
#define VECTOR_PREFIX sockaddr
#define DEFAULT_ALLOC_SIZE 2
#include "vector.h"

struct options {
    int tunnel;                    /* Tunnel type: layer 2 or 3 */
    int tunnel_index;              /* Force the device instance number */
    int mode;                      /* Server mode: router, switch or hub */

    int debug;                     /* If true debug is allowed */
    int compression;               /* If true compression is allowed */
    int encryption;                /* If true encryption is allowed */

    int ports[TNETACLE_MAX_PORTS]; /* Port number to listen on */
    int cports[TNETACLE_MAX_PORTS];/* Port number to listen on, for clients */
    int addr_family;               /* Address family used by the server */
    /* Addresses on which the server listens */
    struct vector_sockaddr *listen_addrs;
    /* Addresses for the client */
    struct vector_sockaddr *client_addrs;
    /* Addresses of others tNETacle daemons */
    struct vector_sockaddr *peer_addrs;
    char *addr;                    /* Address on the VPN */

    const char *key_path;
    const char *cert_path;
};

enum {
    TNT_TUNMODE_TUNNEL,
    TNT_TUNMODE_ETHERNET
};

enum {
    TNT_DAEMONMODE_ROUTER,
    TNT_DAEMONMODE_SWITCH,
    TNT_DAEMONMODE_HUB
};

#endif

