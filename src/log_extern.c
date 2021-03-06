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

#include <event2/event.h>
#if 0
# include <tuntap.h>
#endif

#include "tnetacle.h"
#include "log.h"

extern int debug;

void
tnet_libevent_log(int severity, const char *msg) {
    switch (severity) {
    case _EVENT_LOG_DEBUG:
        if (debug == 1) {
            log_debug("[libevent]%s", msg);
        }
        break;
    case _EVENT_LOG_MSG:
        log_info("[libevent] %s", msg);
        break;
    case _EVENT_LOG_WARN:
        log_warnx("[libevent] %s", msg);
        break;
    case _EVENT_LOG_ERR:
        /* We will not quit */
        log_warnx("[libevent] ERROR: %s", msg);
        break;
    default:
        log_notice("Received strange log from libevent");
        break;
    }
}

void
tnet_libtuntap_log(int severity, const char *msg) {
#if 0
    switch (severity) {
    case TUNTAP_LOG_DEBUG:
        if (debug == 1) {
            log_debug("[libtuntap]%s", msg);
        }
        break;
    case TUNTAP_LOG_NOTICE:
        log_notice("[libtuntap] %s", msg);
        break;
    case TUNTAP_LOG_INFO:
        log_info("[libtuntap] %s", msg);
        break;
    case TUNTAP_LOG_WARN:
        log_warnx("[libtuntap] %s", msg);
        break;
    case TUNTAP_LOG_ERR:
        /* We will not quit */
        log_warnx("[libtuntap] ERROR: %s", msg);
        break;
    default:
        log_notice("Received strange log from libtuntap");
        break;
    }
#endif
}

void
tnet_libevent_dump(struct event_base *base) {
    int i;
    enum event_method_feature f;
    const char **methods = event_get_supported_methods();

    printf("Starting Libevent %s.  Available methods are:\n",
        event_get_version());
    for (i=0; methods[i] != NULL; ++i) {
        printf("    %s\n", methods[i]);
    }

    printf("Using Libevent with backend method %s.",
        event_base_get_method(base));
    f = event_base_get_features(base);
    if ((f & EV_FEATURE_ET))
        printf("  Edge-triggered events are supported.");
    if ((f & EV_FEATURE_O1))
        printf("  O(1) event notification is supported.");
    if ((f & EV_FEATURE_FDS))
        printf("  All FD types are supported.");
    puts("");
}
