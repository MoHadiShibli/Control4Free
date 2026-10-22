#ifndef C4F_NET_H
#define C4F_NET_H

#include <stddef.h>
#include <stdint.h>
#include "c4f_conn.h"
#include "c4f_rtc.h"

#define C4F_NET_CLIENTS 8
#define C4F_NET_WEBSOCKETS 6
#ifndef C4F_NET_PING_MS
#define C4F_NET_PING_MS 5000
#endif
#ifndef C4F_NET_PONG_MS
#define C4F_NET_PONG_MS 10000
#endif
#define C4F_NET_MESSAGE 4096
typedef struct C4fNetClient {
    C4fConn conn;          /* first: a WebSocket is one of the service's connections */
    int fd, websocket, closing, fragmented, savedOrigin;
    uint64_t openedMs, pingAt, pongDeadline, pingToken;
    unsigned char rx[8192], tx[8192];
    size_t rxUsed, txUsed, txSent, messageUsed;
    char message[C4F_NET_MESSAGE + 1];
    const unsigned char *body;
    size_t bodySize, bodySent;
    void *user;
#ifdef C4F_DIAG
    char peer[16]; /* the other end's address, for the diagnostic log */
#endif
} C4fNetClient;

typedef struct {
    int fd;
    uint64_t checkedMs;
    C4fNetClient clients[C4F_NET_CLIENTS];
    C4fNetHandler handler;
    void *context;
    C4fRtc rtc;            /* the pages' WebRTC peers, on the same port number over UDP */
} C4fNet;

/* Opens the HTTP listener and the WebRTC port. A certificate made by an
 * earlier open is kept. */
int c4fNetOpen(C4fNet *, int port, C4fNetHandler, void *);
/* -1: listener/select failure; -2: long pause inside select. Reopen transport. */
int c4fNetPoll(C4fNet *, int timeoutMs);
void c4fNetClose(C4fNet *);
#ifdef C4F_TEST_WEBSOCKET
int c4fNetText(C4fNetClient *, const char *);
void c4fWebSocketAccept(const char *key, char out[29]);
#endif
/* The JSON answer to a launcher request (a C4F_NET_HTTP_* event). */
void c4fNetHttpJson(C4fConn *, const char *);

extern const unsigned char c4fPage[];
extern const size_t c4fPageSize;
/* The console's own address on the network it reaches the internet through, as
 * "192.168.1.20", or an empty string if it cannot be worked out. Nothing is sent. */
void c4fNetLocalAddress(char *out, size_t size);

/* Served so the page can be kept on a phone's home screen (tools/embed_file.py). */
extern const unsigned char c4fManifest[];
extern const size_t c4fManifestSize;
extern const unsigned char c4fIcon[];
extern const size_t c4fIconSize;
#endif
