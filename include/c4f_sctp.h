#ifndef C4F_SCTP_H
#define C4F_SCTP_H
/* A minimal SCTP association for WebRTC data channels, carried inside DTLS
 * (RFC 9260, RFC 8261, RFC 8831). The browser opens it with INIT; the PS4 only
 * answers. Two negotiated channels, so no DCEP: stream 0 is reliable and in
 * order, stream 1 unordered and never resent (partial reliability, RFC 3758).
 *
 * No sockets and no clock of its own: packets go in through c4fSctpReceive,
 * out through ops.send, and the caller passes the time. */
#include <stddef.h>
#include <stdint.h>

#define C4F_SCTP_MTU 1150            /* the biggest packet sent: DTLS adds ~40 bytes under WebRTC's 1200 */
#define C4F_SCTP_MAX_MESSAGE 16384   /* the biggest message either way (a=max-message-size) */
#define C4F_SCTP_PPID_STRING 51
#define C4F_SCTP_PPID_BINARY 53
#define C4F_SCTP_PPID_STRING_EMPTY 56
#define C4F_SCTP_PPID_BINARY_EMPTY 57

typedef struct {
    void *user;
    /* One SCTP packet to send (inside a DTLS record). The receiver copies or
     * sends it before returning: 0 accepted, -1 fatal. */
    int (*send)(void *user, const uint8_t *packet, size_t len);
    /* One whole message. The association may be used (sent on) from here. */
    void (*deliver)(void *user, uint16_t stream, uint32_t ppid, const uint8_t *data, size_t len);
    /* Unpredictable bytes, for tags and the first TSN. */
    void (*random)(void *user, void *out, size_t len);
} C4fSctpOps;

typedef struct C4fSctpOut C4fSctpOut;   /* a sent chunk waiting for its acknowledgement */
typedef struct C4fSctpIn C4fSctpIn;     /* a received chunk waiting for the rest of its message or its turn */

enum { C4F_SCTP_CLOSED, C4F_SCTP_COOKIE_WAIT, C4F_SCTP_ESTABLISHED, C4F_SCTP_ENDED };

typedef struct {
    C4fSctpOps ops;
    int state;
    uint16_t localPort, remotePort;
    uint32_t localTag, remoteTag;
    uint32_t localInitialTsn, remoteInitialTsn;
    uint8_t cookie[16];

    /* Receiving */
    uint32_t cumTsn;                 /* every TSN up to here has arrived */
    uint32_t highTsn;                /* the highest arrived */
    uint8_t seen[128];               /* TSNs cumTsn+1 .. cumTsn+1024, one bit each, by TSN % 1024 */
    uint16_t nextSsn[2];             /* the next message due on an ordered stream */
    C4fSctpIn *in;                   /* waiting chunks, by TSN */
    size_t inBytes;
    int sackDue;

    /* Sending */
    uint32_t nextTsn;
    uint16_t ssn[2];
    C4fSctpOut *out, *outTail;       /* in TSN order */
    size_t outBytes;
    unsigned outChunks;
    uint32_t peerCumTsn;             /* the browser has everything up to here */
    uint32_t highestSent;            /* unsent queued chunks cannot be acknowledged */
    uint32_t forwardTsn;             /* given up on (or acknowledged) up to here */
    uint32_t peerRwnd, cwnd, ssthresh, partialBytesAcked;
    uint32_t flightBytes, flightData, recoveryTsn;
    int fastRecovery;
    uint64_t rto, srtt, rttvar;
    int rttKnown;
    uint64_t t3;                     /* when the oldest unacknowledged chunk is due, 0 when nothing is */
    uint64_t probeAt;                /* a closed receive window is probed after one RTO */
    uint64_t sendAt;                 /* continue a bounded burst on the next service turn */
    unsigned timeouts;
    uint32_t reconfigSeq;

    /* For the log */
    uint64_t packetsIn, packetsOut, resent, abandoned, duplicates, badChecksums;
} C4fSctp;

void c4fSctpInit(C4fSctp *, const C4fSctpOps *ops);
void c4fSctpFree(C4fSctp *);
/* One SCTP packet, as it came out of DTLS. */
void c4fSctpReceive(C4fSctp *, const uint8_t *packet, size_t len, uint64_t now);
/* Queues stream 0 reliably and in order, subject to congestion/receive windows.
 * Stream 1 is a single datagram, sent once; -1 when blocked so the application
 * can replace it with fresh state instead of buffering old rumble/input.
 * 0 accepted, -1 when not established, too big, blocked or queue full. */
int c4fSctpSend(C4fSctp *, uint16_t stream, uint32_t ppid, const uint8_t *data, size_t len, uint64_t now);
/* Resends what is overdue and gives up on what may be lost. */
void c4fSctpTick(C4fSctp *, uint64_t now);
/* When c4fSctpTick next has work, or 0 for nothing pending. */
uint64_t c4fSctpDeadline(const C4fSctp *);
uint32_t c4fCrc32c(const uint8_t *data, size_t len);
#endif
