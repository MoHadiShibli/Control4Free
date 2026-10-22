#ifndef C4F_RTC_H
#define C4F_RTC_H
/* WebRTC for the controller page: the PS4 end of a data-channel connection.
 *
 * The page posts its offer to POST /rtc and gets the answer. From then on
 * everything goes over UDP: ICE-lite connectivity checks (STUN), DTLS 1.2, and
 * SCTP with two negotiated channels, 0 for replies and status (reliable, in
 * order) and 1 for input and rumble (newest wins, never resent). A lost packet
 * on channel 1 is simply skipped, so it can't hold up the ones after it. */
#include <stddef.h>
#include <stdint.h>
#include "c4f_conn.h"

#define C4F_RTC_PEERS 8
#define C4F_RTC_SILENT_MS 10000   /* nothing from a peer for this long: gone */
#define C4F_RTC_SETUP_MS 20000    /* an offer that never got as far as SCTP */

typedef struct C4fRtcPeer C4fRtcPeer;
typedef struct C4fRtcCrypto C4fRtcCrypto;

typedef struct {
    int fd;                        /* UDP, or -1 */
    int port;
    C4fRtcCrypto *crypto;          /* certificate, DTLS settings, random numbers: made once */
    char fingerprint[96];          /* our certificate's SHA-256, "AB:CD:...", for answers */
    C4fRtcPeer *peers[C4F_RTC_PEERS];
    C4fNetHandler handler;
    void *context;
    unsigned nextId;
    unsigned writeCursor;          /* rotate the first peer drained on UDP writability */
} C4fRtc;

/* Makes the certificate the first time, then opens the UDP port. 0 or -1. */
int c4fRtcOpen(C4fRtc *, int port, C4fNetHandler, void *context);
/* Closes every peer (those that were open get a close event) and the socket.
 * The certificate is kept for the next open. */
void c4fRtcClose(C4fRtc *);
/* Permanent service shutdown: close peers/socket and release certificate state. */
void c4fRtcDestroy(C4fRtc *);
/* An offer from POST /rtc. `local` is the console's address the page reached,
 * in network byte order: the only candidate the answer offers. Writes the
 * answer and returns 0, or an HTTP status: 400 for an offer that isn't one,
 * 503 when no peer slot is free. */
int c4fRtcOffer(C4fRtc *, const char *offer, size_t len, uint32_t local, char *answer, size_t size);
/* Reads every datagram waiting on the socket. */
void c4fRtcReadable(C4fRtc *);
/* Register UDP writability only while a nonblocking DTLS write is pending. */
int c4fRtcWantsWrite(const C4fRtc *);
void c4fRtcWritable(C4fRtc *);
/* DTLS and SCTP resends, silent and failed peers. */
void c4fRtcTick(C4fRtc *, uint64_t now);
/* Milliseconds until c4fRtcTick has work, at most `limit`. */
int c4fRtcWait(const C4fRtc *, uint64_t now, int limit);
#endif
