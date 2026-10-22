#ifndef C4F_CONN_H
#define C4F_CONN_H
/* A page's connection, whichever way it arrived: a WebRTC peer (the product)
 * or a WebSocket (host tests only). The service treats both the same way. */
#include <stddef.h>
#include <stdint.h>

typedef struct C4fConn C4fConn;
struct C4fConn {
    /* Reliable and ordered while connected: replies, status, errors. 0, or -1 when it
     * could not be queued (the connection is then closed). */
    int (*text)(C4fConn *, const char *);
    /* The newest state wins and a lost one is not resent: rumble. NULL means
     * the connection has no such path and text is used. */
    int (*input)(C4fConn *, const char *);
};

static inline int c4fConnText(C4fConn *c, const char *text) { return c->text(c, text); }
static inline int c4fConnInput(C4fConn *c, const char *text) { return (c->input ? c->input : c->text)(c, text); }

enum { C4F_NET_OPEN, C4F_NET_MESSAGE_EVENT, C4F_NET_INPUT_EVENT, C4F_NET_CLOSE,
       C4F_NET_HTTP_STATUS, C4F_NET_HTTP_STOP };
typedef void (*C4fNetHandler)(C4fConn *, int event, const char *text, size_t len, void *context);

/* Milliseconds on a clock that only moves forward (net.c). */
uint64_t c4fTimeMs(void);
#endif
