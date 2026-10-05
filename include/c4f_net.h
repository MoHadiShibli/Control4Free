#ifndef C4F_NET_H
#define C4F_NET_H

#include <stddef.h>
#include <stdint.h>

#define C4F_NET_CLIENTS 8
#define C4F_NET_MESSAGE 4096
typedef struct C4fNetClient {
    int fd, websocket, closing, fragmented;
    uint64_t openedMs;
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

enum { C4F_NET_OPEN, C4F_NET_MESSAGE_EVENT, C4F_NET_CLOSE,
       C4F_NET_HTTP_STATUS, C4F_NET_HTTP_STOP };
typedef void (*C4fNetHandler)(C4fNetClient *, int, const char *, size_t, void *);
typedef struct {
    int fd;
    uint64_t checkedMs;
    C4fNetClient clients[C4F_NET_CLIENTS];
    C4fNetHandler handler;
    void *context;
} C4fNet;

uint64_t c4fTimeMs(void);
int c4fNetOpen(C4fNet *, int port, C4fNetHandler, void *);
/* -1: listener/select failure; -2: long pause inside select. Reopen transport. */
int c4fNetPoll(C4fNet *, int timeoutMs);
void c4fNetClose(C4fNet *);
int c4fNetText(C4fNetClient *, const char *);
void c4fNetHttpJson(C4fNetClient *, const char *);
void c4fWebSocketAccept(const char *key, char out[29]);

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
