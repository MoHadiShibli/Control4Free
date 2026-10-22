/* Deterministic tests of the real RTC datagram boundary. No sockets or PS4:
 * force the sendto outcomes that are difficult to reproduce on a healthy LAN. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static ssize_t c4fTestSendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
#define sendto c4fTestSendto
#include "../src/rtc.c"
#undef sendto

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

static uint64_t fakeNow = 1000;
static int sendError;
static unsigned writes;
static uint8_t wire[256][C4F_RTC_MTU];
static size_t wireLen[256];
static uint16_t wirePort[256];

uint64_t c4fTimeMs(void) { return fakeNow; }
void c4fLog(const char *fmt, ...) { (void)fmt; }

static ssize_t c4fTestSendto(int fd, const void *data, size_t len, int flags,
                            const struct sockaddr *to, socklen_t tolen)
{
    (void)fd; (void)flags; (void)tolen;
    if (sendError) { errno = sendError; return -1; }
    CHECK(writes < 256 && len <= C4F_RTC_MTU);
    memcpy(wire[writes], data, len); wireLen[writes] = len;
    wirePort[writes] = ((const struct sockaddr_in *)to)->sin_port;
    writes++;
    return (ssize_t)len;
}

static C4fRtcPeer *peer(C4fRtc *rtc, unsigned slot)
{
    C4fRtcPeer *p = calloc(1, sizeof(*p));
    CHECK(p);
    p->rtc = rtc; p->addrKnown = 1; p->addr.sin_family = AF_INET; p->addr.sin_port = htons((uint16_t)(6000 + slot));
    rtc->peers[slot] = p;
    return p;
}

static void dispose(C4fRtc *rtc)
{
    for (unsigned i = 0; i < C4F_RTC_PEERS; i++) {
        C4fRtcPeer *p = rtc->peers[i];
        if (!p) continue;
        while (p->out) { C4fRtcDatagram *d = p->out; p->out = d->next; free(d); }
        free(p); rtc->peers[i] = NULL;
    }
}

int main(void)
{
    C4fRtc rtc = { .fd = 77 };
    C4fRtcPeer *p = peer(&rtc, 0);
    uint8_t a[] = { 1, 2, 3 }, b[] = { 4, 5, 6, 7 };
    sendError = EAGAIN;
    CHECK(c4fRtcBioSend(p, a, sizeof(a)) == sizeof(a));
    CHECK(c4fRtcBioSend(p, b, sizeof(b)) == sizeof(b));
    CHECK(p->outPackets == 2 && !p->datagramsOut && c4fRtcWantsWrite(&rtc));
    memset(a, 99, sizeof(a)); memset(b, 99, sizeof(b)); /* caller's buffers can be reused immediately */
    c4fRtcWritable(&rtc);
    CHECK(p->outPackets == 2 && !writes);
    sendError = 0;
    c4fRtcWritable(&rtc);
    CHECK(writes == 2 && wireLen[0] == 3 && wire[0][0] == 1 && wire[1][0] == 4);
    CHECK(!p->outPackets && p->datagramsOut == 2 && !c4fRtcWantsWrite(&rtc));
    CHECK(wirePort[0] == htons(6000));
    CHECK(c4fRtcBioSend(p, a, sizeof(a)) == sizeof(a) && writes == 3 && wire[2][0] == 99);

    /* ENOBUFS can occur on a writable UDP socket. Back off briefly instead of
     * spinning select continuously or pretending that the datagram was sent. */
    sendError = ENOBUFS;
    CHECK(c4fRtcBioSend(p, a, sizeof(a)) == sizeof(a) && !c4fRtcWantsWrite(&rtc));
    CHECK(c4fRtcWait(&rtc, fakeNow, 50) <= 2);
    fakeNow += 2;
    CHECK(c4fRtcWantsWrite(&rtc));
    CHECK(c4fRtcWait(&rtc, fakeNow, 50) > 0); /* expired backoff must not force a busy select loop */
    sendError = 0; c4fRtcWritable(&rtc);
    CHECK(!p->out && writes == 4);

    /* Bounded queues fail explicitly, and permanent socket failures are not
     * counted as successful datagrams. */
    sendError = EAGAIN;
    for (unsigned i = 0; i < C4F_RTC_OUT_PACKETS; i++) CHECK(c4fRtcBioSend(p, a, sizeof(a)) == sizeof(a));
    CHECK(c4fRtcBioSend(p, a, sizeof(a)) < 0 && p->failed && p->outPackets == C4F_RTC_OUT_PACKETS);
    CHECK(p->datagramsOut == 4);
    dispose(&rtc);

    p = peer(&rtc, 0);
    sendError = ECONNREFUSED;
    CHECK(c4fRtcBioSend(p, a, sizeof(a)) < 0 && p->failed && !p->out && !p->datagramsOut);
    dispose(&rtc);

    /* Each peer gets a bounded drain and the first served peer rotates. */
    p = peer(&rtc, 0);
    C4fRtcPeer *other = peer(&rtc, 1);
    sendError = EAGAIN;
    for (unsigned i = 0; i < 12; i++) {
        CHECK(c4fRtcBioSend(p, a, sizeof(a)) == sizeof(a));
        CHECK(c4fRtcBioSend(other, b, sizeof(b)) == sizeof(b));
    }
    sendError = 0; c4fRtcWritable(&rtc);
    CHECK(p->outPackets == 4 && other->outPackets == 4);
    c4fRtcWritable(&rtc);
    CHECK(!p->out && !other->out);
    dispose(&rtc);
    puts("PASS rtc I/O: owned ciphertext retry/order, EAGAIN/ENOBUFS, bounded queues, fatal errors and fair peer drains");
    return 0;
}
