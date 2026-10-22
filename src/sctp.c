/* A minimal SCTP association for WebRTC data channels. See c4f_sctp.h.
 *
 * What it does, and no more:
 * - answers the browser's INIT (INIT-ACK with a cookie, COOKIE-ECHO, COOKIE-ACK);
 * - receives DATA on streams 0 (reliable, in order) and 1 (unordered), puts
 *   fragments back together, and acknowledges every packet straight away;
 * - sends on stream 0 reliably (resent after a timeout, or sooner when three
 *   acknowledgements in a row show it missing) and on stream 1 once (given up
 *   on instead, and skipped over with FORWARD-TSN);
 * - answers HEARTBEAT, stream resets (RE-CONFIG) and FORWARD-TSN, and ends on
 *   ABORT or SHUTDOWN.
 * New DATA observes a shared congestion window and the peer's receive window.
 * Reliable messages wait in a bounded queue; blocked unreliable state is
 * rejected for the caller to replace. No message is delayed to batch it. */
#include <stdlib.h>
#include <string.h>

#include "c4f_sctp.h"

enum {
    CHUNK_DATA = 0, CHUNK_INIT = 1, CHUNK_INIT_ACK = 2, CHUNK_SACK = 3, CHUNK_HEARTBEAT = 4,
    CHUNK_HEARTBEAT_ACK = 5, CHUNK_ABORT = 6, CHUNK_SHUTDOWN = 7, CHUNK_SHUTDOWN_ACK = 8,
    CHUNK_ERROR = 9, CHUNK_COOKIE_ECHO = 10, CHUNK_COOKIE_ACK = 11, CHUNK_SHUTDOWN_COMPLETE = 14,
    CHUNK_RECONFIG = 130, CHUNK_FORWARD_TSN = 192,
};
enum { DATA_END = 1, DATA_BEGIN = 2, DATA_UNORDERED = 4 };
enum { PARAM_STATE_COOKIE = 7, PARAM_OUTGOING_RESET = 13, PARAM_INCOMING_RESET = 14,
       PARAM_RECONFIG_RESPONSE = 16, PARAM_SUPPORTED_EXTENSIONS = 0x8008, PARAM_FORWARD_TSN = 0xC000 };
enum { RECONFIG_PERFORMED = 1, RECONFIG_DENIED = 2 };

#define C4F_SCTP_RWND (128u * 1024u)       /* what we take in before delivering */
#define C4F_SCTP_OUT_LIMIT (256u * 1024u)  /* what may wait for acknowledgement */
#define C4F_SCTP_RTO_START 200
#define C4F_SCTP_RTO_MIN 100
#define C4F_SCTP_RTO_MAX 1000
#define C4F_SCTP_WINDOW 1024                /* TSNs tracked beyond the cumulative one */
#define C4F_SCTP_MAX_GAPS 32
#define C4F_SCTP_OUT_CHUNKS 512
#define C4F_SCTP_MAX_BURST 4
#define C4F_SCTP_MAX_TIMEOUTS 8
#define C4F_SCTP_DATA_MAX ((C4F_SCTP_MTU - 28u) & ~3u)
#define C4F_SCTP_INITIAL_CWND 4380u

struct C4fSctpOut {
    C4fSctpOut *next;
    uint32_t tsn, ppid;
    uint16_t stream, ssn, len;
    uint8_t flags, reliable, acked, abandoned, fastDone, inFlight;
    int missing;
    unsigned sends;
    uint64_t sentAt;
    uint8_t data[];                          /* retained until cumulative acknowledgement */
};

struct C4fSctpIn {
    C4fSctpIn *next;
    uint32_t tsn, ppid;
    uint16_t stream, ssn, len;
    uint8_t flags;
    uint8_t data[];
};

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
/* Serial number arithmetic (RFC 1982), as TSNs wrap. */
static int tsnLt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static int tsnLe(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static uint32_t c4fCrc32cUpdate(uint32_t c, const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0x82F63B78u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        ready = 1;
    }
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c;
}

uint32_t c4fCrc32c(const uint8_t *data, size_t len)
{
    return c4fCrc32cUpdate(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------ packets */

typedef struct { uint8_t buf[C4F_SCTP_MTU]; size_t len; } C4fPacket;

static void pktStart(const C4fSctp *s, C4fPacket *p, uint32_t tag)
{
    put16(p->buf, s->localPort); put16(p->buf + 2, s->remotePort);
    put32(p->buf + 4, tag); put32(p->buf + 8, 0);
    p->len = 12;
}

static size_t pktRoom(const C4fPacket *p) { return C4F_SCTP_MTU - p->len; }

/* Adds a chunk header; the value goes right after it. NULL when it doesn't fit. */
static uint8_t *pktChunk(C4fPacket *p, uint8_t type, uint8_t flags, size_t valueLen)
{
    size_t total = (4 + valueLen + 3) & ~(size_t)3;
    if (total > pktRoom(p)) return NULL;
    uint8_t *c = p->buf + p->len;
    memset(c, 0, total);
    c[0] = type; c[1] = flags; put16(c + 2, (uint16_t)(4 + valueLen));
    p->len += total;
    return c + 4;
}

static int pktSend(C4fSctp *s, C4fPacket *p)
{
    if (p->len <= 12) return 0;
    uint32_t crc = c4fCrc32c(p->buf, p->len);
    /* The checksum goes in least significant byte first (RFC 9260, appendix A). */
    p->buf[8] = (uint8_t)crc; p->buf[9] = (uint8_t)(crc >> 8); p->buf[10] = (uint8_t)(crc >> 16); p->buf[11] = (uint8_t)(crc >> 24);
    if (s->ops.send(s->ops.user, p->buf, p->len)) { s->state = C4F_SCTP_ENDED; return -1; }
    s->packetsOut++;
    pktStart(s, p, get32(p->buf + 4));
    return 0;
}

/* ------------------------------------------------------------ receiving */

static int seenGet(const C4fSctp *s, uint32_t tsn) { uint32_t i = tsn % C4F_SCTP_WINDOW; return s->seen[i >> 3] >> (i & 7) & 1; }
static void seenSet(C4fSctp *s, uint32_t tsn, int on)
{
    uint32_t i = tsn % C4F_SCTP_WINDOW;
    if (on) s->seen[i >> 3] |= (uint8_t)(1u << (i & 7)); else s->seen[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

static void advanceCum(C4fSctp *s)
{
    while (seenGet(s, s->cumTsn + 1)) { seenSet(s, s->cumTsn + 1, 0); s->cumTsn++; }
    if (tsnLt(s->highTsn, s->cumTsn)) s->highTsn = s->cumTsn;
}

/* Adds a SACK for what has arrived so far. */
static int addSack(C4fSctp *s, C4fPacket *p)
{
    uint16_t gaps[C4F_SCTP_MAX_GAPS][2];
    int count = 0;
    uint32_t t = s->cumTsn + 1;
    while (tsnLe(t, s->highTsn) && count < C4F_SCTP_MAX_GAPS) {
        while (tsnLe(t, s->highTsn) && !seenGet(s, t)) t++;
        if (tsnLt(s->highTsn, t)) break;
        uint32_t start = t;
        while (tsnLe(t, s->highTsn) && seenGet(s, t)) t++;
        gaps[count][0] = (uint16_t)(start - s->cumTsn);
        gaps[count][1] = (uint16_t)(t - 1 - s->cumTsn);
        count++;
    }
    uint8_t *v = pktChunk(p, CHUNK_SACK, 0, 12 + 4 * (size_t)count);
    if (!v) return -1;
    put32(v, s->cumTsn);
    put32(v + 4, s->inBytes < C4F_SCTP_RWND ? C4F_SCTP_RWND - (uint32_t)s->inBytes : 0);
    put16(v + 8, (uint16_t)count); put16(v + 10, 0);
    for (int i = 0; i < count; i++) { put16(v + 12 + 4 * i, gaps[i][0]); put16(v + 14 + 4 * i, gaps[i][1]); }
    s->sackDue = 0;
    return 0;
}

static void freeIn(C4fSctp *s, C4fSctpIn *e)
{
    s->inBytes -= e->len;
    free(e);
}

/* The message starting at `first` (a BEGIN chunk), if all of it is here: its
 * last chunk, else NULL. Chunks are kept in TSN order. */
static C4fSctpIn *messageEnd(C4fSctpIn *first)
{
    C4fSctpIn *e = first;
    for (;;) {
        if (e->flags & DATA_END) return e;
        C4fSctpIn *n = e->next;
        if (!n || n->tsn != e->tsn + 1 || n->stream != first->stream ||
            (n->flags & (DATA_BEGIN | DATA_UNORDERED)) != (first->flags & DATA_UNORDERED) ||
            (!(first->flags & DATA_UNORDERED) && n->ssn != first->ssn)) return NULL;
        e = n;
    }
}

/* Takes a whole message out of the list and hands it over. */
static void deliverRun(C4fSctp *s, C4fSctpIn **link, C4fSctpIn *last)
{
    C4fSctpIn *first = *link;
    size_t total = 0;
    for (C4fSctpIn *e = first; ; e = e->next) { total += e->len; if (e == last) break; }
    *link = last->next;
    uint8_t *message = total <= C4F_SCTP_MAX_MESSAGE ? malloc(total ? total : 1) : NULL;
    size_t at = 0;
    uint16_t stream = first->stream;
    uint32_t ppid = first->ppid;
    for (C4fSctpIn *e = first, *next; ; e = next) {
        next = e->next;
        int end = e == last;
        if (message) memcpy(message + at, e->data, e->len);
        at += e->len;
        freeIn(s, e);
        if (end) break;
    }
    if (message) {
        s->ops.deliver(s->ops.user, stream, ppid, message, total);
        free(message);
    } else s->state = C4F_SCTP_ENDED; /* acknowledged reliable data must not disappear silently */
}

/* Hands over every message that is complete and due. */
static void deliverReady(C4fSctp *s)
{
    for (int progressed = 1; progressed; ) {
        progressed = 0;
        for (C4fSctpIn **link = &s->in; *link; link = &(*link)->next) {
            C4fSctpIn *e = *link;
            if (!(e->flags & DATA_BEGIN)) continue;
            if (!(e->flags & DATA_UNORDERED) && (e->stream > 1 || e->ssn != s->nextSsn[e->stream])) continue;
            C4fSctpIn *last = messageEnd(e);
            if (!last) continue;
            if (!(e->flags & DATA_UNORDERED)) s->nextSsn[e->stream]++;
            deliverRun(s, link, last);
            progressed = 1;
            break;
        }
    }
}

static void onData(C4fSctp *s, uint8_t flags, const uint8_t *v, size_t vlen)
{
    if (s->state != C4F_SCTP_ESTABLISHED || vlen < 12) return;
    uint32_t tsn = get32(v), ppid = get32(v + 8);
    uint16_t stream = get16(v + 4), ssn = get16(v + 6);
    const uint8_t *payload = v + 12;
    size_t plen = vlen - 12;
    s->sackDue = 1;
    if (tsnLe(tsn, s->cumTsn)) { s->duplicates++; return; }
    if (tsn - s->cumTsn > C4F_SCTP_WINDOW) return;           /* beyond what we track: it comes again */
    if (seenGet(s, tsn)) { s->duplicates++; return; }
    int unordered = flags & DATA_UNORDERED, whole = (flags & (DATA_BEGIN | DATA_END)) == (DATA_BEGIN | DATA_END);
    int stale = !unordered && stream <= 1 && (int16_t)(ssn - s->nextSsn[stream]) < 0;
    if (stream > 1 || plen == 0 || plen > C4F_SCTP_MAX_MESSAGE || stale) {
        /* Not one of ours, or a message already delivered: acknowledged and dropped. */
        seenSet(s, tsn, 1);
    } else if (whole && (unordered || ssn == s->nextSsn[stream])) {
        seenSet(s, tsn, 1);
        if (tsnLt(s->highTsn, tsn)) s->highTsn = tsn;
        advanceCum(s);
        if (!unordered) s->nextSsn[stream]++;
        s->ops.deliver(s->ops.user, stream, ppid, payload, plen);
        deliverReady(s);
        return;
    } else {
        if (s->inBytes + plen > C4F_SCTP_RWND) return;        /* full: not acknowledged, so it comes again */
        C4fSctpIn *e = malloc(sizeof(*e) + plen);
        if (!e) return;
        e->tsn = tsn; e->ppid = ppid; e->stream = stream; e->ssn = ssn; e->len = (uint16_t)plen; e->flags = flags;
        memcpy(e->data, payload, plen);
        C4fSctpIn **link = &s->in;
        while (*link && tsnLt((*link)->tsn, tsn)) link = &(*link)->next;
        e->next = *link; *link = e;
        s->inBytes += plen;
        seenSet(s, tsn, 1);
    }
    if (tsnLt(s->highTsn, tsn)) s->highTsn = tsn;
    advanceCum(s);
    deliverReady(s);
}

static void onForwardTsn(C4fSctp *s, const uint8_t *v, size_t vlen)
{
    if (s->state != C4F_SCTP_ESTABLISHED || vlen < 4) return;
    uint32_t target = get32(v);
    s->sackDue = 1;
    if (tsnLe(target, s->cumTsn)) return;
    if (target - s->cumTsn >= C4F_SCTP_WINDOW) memset(s->seen, 0, sizeof(s->seen));
    else for (uint32_t t = s->cumTsn + 1; tsnLe(t, target); t++) seenSet(s, t, 0);
    s->cumTsn = target;
    if (tsnLt(s->highTsn, target)) s->highTsn = target;
    /* What it skipped can never be completed now. */
    for (C4fSctpIn **link = &s->in; *link; ) {
        if (tsnLe((*link)->tsn, target)) { C4fSctpIn *e = *link; *link = e->next; freeIn(s, e); }
        else link = &(*link)->next;
    }
    for (size_t at = 4; at + 4 <= vlen; at += 4) {
        uint16_t stream = get16(v + at), ssn = get16(v + at + 2);
        if (stream <= 1 && (int16_t)(ssn + 1 - s->nextSsn[stream]) > 0) s->nextSsn[stream] = ssn + 1;
    }
    advanceCum(s);
    deliverReady(s);
}

/* ------------------------------------------------------------ sending */

static void addData(C4fPacket *p, const C4fSctpOut *o, const uint8_t *data)
{
    uint8_t *v = pktChunk(p, CHUNK_DATA, o->flags, 12 + (size_t)o->len);
    put32(v, o->tsn); put16(v + 4, o->stream); put16(v + 6, o->ssn); put32(v + 8, o->ppid);
    memcpy(v + 12, data, o->len);
}

static uint32_t dataSize(const C4fSctpOut *o) { return (16u + o->len + 3u) & ~3u; }

static void releaseFlight(C4fSctp *s, C4fSctpOut *o)
{
    if (!o->inFlight) return;
    s->flightBytes -= dataSize(o); s->flightData -= o->len; o->inFlight = 0;
}

static void addFlight(C4fSctp *s, C4fSctpOut *o)
{
    if (o->inFlight) return;
    s->flightBytes += dataSize(o); s->flightData += o->len; o->inFlight = 1;
}

static void retransmit(C4fSctp *s, C4fSctpOut *o, C4fPacket *p, uint64_t now)
{
    if (pktRoom(p) < dataSize(o) && pktSend(s, p)) return;
    addData(p, o, o->data);
    o->sends++; o->sentAt = now; o->missing = 0;
    addFlight(s, o);
    s->resent++;
    for (C4fSctpOut *first = s->out; first; first = first->next) {
        if (!first->sends || first->acked || first->abandoned) continue;
        if (first == o) s->t3 = now + s->rto;
        break;
    }
}

static void removeOut(C4fSctp *s, C4fSctpOut **link)
{
    C4fSctpOut *o = *link;
    *link = o->next;
    if (s->outTail == o) {
        s->outTail = NULL;
        for (C4fSctpOut *e = s->out; e; e = e->next) s->outTail = e;
    }
    releaseFlight(s, o);
    s->outBytes -= o->len; s->outChunks--;
    free(o);
}

/* Moves the forward point over everything acknowledged or given up on. */
static int advanceForward(C4fSctp *s)
{
    int skipped = 0;
    if (tsnLt(s->forwardTsn, s->peerCumTsn)) s->forwardTsn = s->peerCumTsn;
    for (C4fSctpOut *o = s->out; o; o = o->next) {
        if (tsnLe(o->tsn, s->forwardTsn)) { skipped |= o->abandoned; continue; }
        if (!o->sends || o->tsn != s->forwardTsn + 1 || !(o->acked || o->abandoned)) break;
        s->forwardTsn = o->tsn;
        skipped |= o->abandoned;
    }
    return skipped && tsnLt(s->peerCumTsn, s->forwardTsn);
}

static void addForwardTsn(C4fSctp *s, C4fPacket *p)
{
    uint8_t *v = pktChunk(p, CHUNK_FORWARD_TSN, 0, 4);
    if (!v) { if (pktSend(s, p)) return; v = pktChunk(p, CHUNK_FORWARD_TSN, 0, 4); }
    put32(v, s->forwardTsn);
}

static int anyOutstanding(const C4fSctp *s)
{
    for (const C4fSctpOut *o = s->out; o; o = o->next) if (o->sends && !o->acked && !o->abandoned) return 1;
    return tsnLt(s->peerCumTsn, s->forwardTsn);
}

static int hasQueued(const C4fSctp *s)
{
    for (const C4fSctpOut *o = s->out; o; o = o->next) if (!o->sends) return 1;
    return 0;
}

/* Release up to Max.Burst packets without delaying the first for batching.
 * A closed (or too-small) peer window gets one DATA probe after an RTO. */
static void pump(C4fSctp *s, uint64_t now)
{
    unsigned burst = 0;
    s->sendAt = 0;
    for (C4fSctpOut *o = s->out; o && burst < C4F_SCTP_MAX_BURST && s->state == C4F_SCTP_ESTABLISHED; o = o->next) {
        if (o->sends) continue;
        uint32_t room = s->peerRwnd > s->flightData ? s->peerRwnd - s->flightData : 0;
        if (s->flightBytes + dataSize(o) > s->cwnd) break;
        int probe = o->len > room;
        if (probe) {
            if (s->flightBytes) break;
            if (!s->probeAt) s->probeAt = now + s->rto;
            if (now < s->probeAt) break;
        } else s->probeAt = 0;
        C4fPacket p;
        pktStart(s, &p, s->remoteTag);
        if (s->sackDue) addSack(s, &p);
        if (pktRoom(&p) < dataSize(o) && pktSend(s, &p)) return;
        addData(&p, o, o->data);
        if (pktSend(s, &p)) return;
        o->sends = 1; o->sentAt = now;
        s->highestSent = o->tsn;
        addFlight(s, o);
        if (!s->t3) s->t3 = now + s->rto;
        burst++;
        if (probe) { s->probeAt = 0; break; }
    }
    if (burst == C4F_SCTP_MAX_BURST) {
        for (C4fSctpOut *o = s->out; o; o = o->next) if (!o->sends) {
            uint32_t room = s->peerRwnd > s->flightData ? s->peerRwnd - s->flightData : 0;
            if (o->len <= room && s->flightBytes + dataSize(o) <= s->cwnd) s->sendAt = now + 1;
            break;
        }
    }
}

static void reduceWindow(C4fSctp *s, int timeout)
{
    s->ssthresh = s->cwnd / 2;
    if (s->ssthresh < 4u * C4F_SCTP_MTU) s->ssthresh = 4u * C4F_SCTP_MTU;
    s->cwnd = timeout ? C4F_SCTP_MTU : s->ssthresh;
    s->partialBytesAcked = 0;
}

static int sackHas(const uint8_t *v, size_t gaps, uint32_t cum, uint32_t tsn)
{
    if (tsnLe(tsn, cum)) return 1;
    for (size_t i = 0; i < gaps; i++) {
        uint32_t from = cum + get16(v + 12 + 4 * i), to = cum + get16(v + 14 + 4 * i);
        if (tsnLe(from, tsn) && tsnLe(tsn, to)) return 1;
    }
    return 0;
}

static void onSack(C4fSctp *s, const uint8_t *v, size_t vlen, uint64_t now)
{
    if (s->state != C4F_SCTP_ESTABLISHED || vlen < 12) return;
    uint32_t cum = get32(v);
    size_t gaps = get16(v + 8);
    if (12 + 4 * gaps > vlen || tsnLt(cum, s->peerCumTsn) || tsnLt(s->highestSent, cum)) return;
    uint16_t last = 0;
    for (size_t i = 0; i < gaps; i++) {
        uint16_t from = get16(v + 12 + 4 * i), to = get16(v + 14 + 4 * i);
        if (!from || from <= last || to < from || tsnLt(s->highestSent, cum + to)) return;
        last = to;
    }
    int advanced = tsnLt(s->peerCumTsn, cum);
    uint32_t flightBefore = s->flightBytes, ackBytes = 0, highestNew = cum;
    C4fSctpOut *sample = NULL;
    for (C4fSctpOut *o = s->out; o && o->sends; o = o->next) {
        int acked = sackHas(v, gaps, cum, o->tsn);
        if (acked && !o->acked && !o->abandoned) {
            ackBytes += dataSize(o);
            if (tsnLt(highestNew, o->tsn)) highestNew = o->tsn;
            if (o->sends == 1 && tsnLe(o->tsn, cum)) sample = o;
        }
        if (acked) releaseFlight(s, o);
        else if (o->acked && !o->abandoned) addFlight(s, o); /* receiver reneged a gap ACK */
        o->acked = (uint8_t)acked;
    }
    if (sample && advanced) {
        uint64_t r = now >= sample->sentAt ? now - sample->sentAt : 0;
        if (!s->rttKnown) { s->srtt = r; s->rttvar = r / 2; s->rttKnown = 1; }
        else {
            uint64_t d = s->srtt > r ? s->srtt - r : r - s->srtt;
            s->rttvar = (3 * s->rttvar + d) / 4;
            s->srtt = (7 * s->srtt + r) / 8;
        }
        uint64_t rto = s->srtt + 4 * s->rttvar;
        s->rto = rto < C4F_SCTP_RTO_MIN ? C4F_SCTP_RTO_MIN : rto > C4F_SCTP_RTO_MAX ? C4F_SCTP_RTO_MAX : rto;
    }
    if (s->fastRecovery && tsnLe(s->recoveryTsn, cum)) s->fastRecovery = 0;
    if (advanced && !s->fastRecovery && flightBefore + C4F_SCTP_MTU >= s->cwnd) {
        if (s->cwnd <= s->ssthresh) s->cwnd += ackBytes < C4F_SCTP_MTU ? ackBytes : C4F_SCTP_MTU;
        else {
            s->partialBytesAcked += ackBytes;
            if (s->partialBytesAcked >= s->cwnd) { s->partialBytesAcked -= s->cwnd; s->cwnd += C4F_SCTP_MTU; }
        }
        if (s->cwnd > C4F_SCTP_OUT_LIMIT) s->cwnd = C4F_SCTP_OUT_LIMIT;
    }
    while (s->out && tsnLe(s->out->tsn, cum)) removeOut(s, &s->out);
    s->peerCumTsn = cum;
    s->peerRwnd = get32(v + 4);
    s->timeouts = 0;
    if (s->peerRwnd) s->probeAt = 0;
    C4fPacket p;
    pktStart(s, &p, s->remoteTag);
    int fastSent = 0;
    for (C4fSctpOut *o = s->out; o && o->sends && tsnLt(o->tsn, highestNew); o = o->next) {
        if (o->acked || o->abandoned || o->fastDone) continue;
        /* Only newly acknowledged TSNs report a new hole (HTNA): a repeated
         * copy of the same SACK must not trigger retransmission. */
        if (++o->missing < 3) continue;
        if (!s->fastRecovery) {
            reduceWindow(s, 0); s->fastRecovery = 1; s->recoveryTsn = s->highestSent;
        }
        if (!o->reliable) { releaseFlight(s, o); o->abandoned = 1; o->fastDone = 1; s->abandoned++; }
        else if (!fastSent) {
            o->fastDone = 1; retransmit(s, o, &p, now); fastSent = 1;
        }
    }
    if (advanceForward(s)) addForwardTsn(s, &p);
    if (pktSend(s, &p)) return;
    if (!anyOutstanding(s)) s->t3 = 0;
    else if (advanced || !s->t3) s->t3 = now + s->rto;
    pump(s, now);
}

int c4fSctpSend(C4fSctp *s, uint16_t stream, uint32_t ppid, const uint8_t *data, size_t len, uint64_t now)
{
    if (s->state != C4F_SCTP_ESTABLISHED || stream > 1 || len == 0 || len > C4F_SCTP_MAX_MESSAGE) return -1;
    int reliable = stream == 0;
    unsigned chunks = (unsigned)((len + C4F_SCTP_DATA_MAX - 1) / C4F_SCTP_DATA_MAX);
    if (s->outBytes + len > C4F_SCTP_OUT_LIMIT || s->outChunks + chunks > C4F_SCTP_OUT_CHUNKS) return -1;
    /* Never queue stale unreliable state behind a window or another message. */
    uint32_t room = s->peerRwnd > s->flightData ? s->peerRwnd - s->flightData : 0;
    if (!reliable && (chunks != 1 || hasQueued(s) || len > room ||
        s->flightBytes + ((16u + len + 3u) & ~3u) > s->cwnd)) return -1;
    C4fSctpOut *head = NULL, *tail = NULL;
    size_t at = 0;
    while (at < len) {
        size_t part = len - at < C4F_SCTP_DATA_MAX ? len - at : C4F_SCTP_DATA_MAX;
        C4fSctpOut *o = calloc(1, sizeof(*o) + part);
        if (!o) {
            while (head) { C4fSctpOut *next = head->next; free(head); head = next; }
            return -1; /* no partial message or consumed sequence numbers */
        }
        o->ppid = ppid; o->stream = stream; o->ssn = reliable ? s->ssn[stream] : 0;
        o->len = (uint16_t)part; o->reliable = (uint8_t)reliable;
        o->flags = (uint8_t)((reliable ? 0 : DATA_UNORDERED) | (at == 0 ? DATA_BEGIN : 0) |
                            (at + part == len ? DATA_END : 0));
        memcpy(o->data, data + at, part);
        if (tail) tail->next = o; else head = o;
        tail = o; at += part;
    }
    for (C4fSctpOut *o = head; o; o = o->next) o->tsn = s->nextTsn++;
    if (s->outTail) s->outTail->next = head; else s->out = head;
    s->outTail = tail; s->outBytes += len; s->outChunks += chunks;
    if (reliable) s->ssn[stream]++;
    pump(s, now);
    return s->state == C4F_SCTP_ESTABLISHED ? 0 : -1;
}

void c4fSctpTick(C4fSctp *s, uint64_t now)
{
    if (s->state != C4F_SCTP_ESTABLISHED) return;
    if (s->t3 && now >= s->t3) {
        if (++s->timeouts > C4F_SCTP_MAX_TIMEOUTS) { s->state = C4F_SCTP_ENDED; return; }
        C4fPacket p;
        pktStart(s, &p, s->remoteTag);
        int retransmitted = 0;
        if (s->peerRwnd) reduceWindow(s, 1);
        s->fastRecovery = 0;
        for (C4fSctpOut *o = s->out; o && o->sends; o = o->next) {
            if (o->acked || o->abandoned || now - o->sentAt < s->rto) continue;
            if (!o->reliable) { releaseFlight(s, o); o->abandoned = 1; s->abandoned++; }
            else if (!retransmitted) {
                o->fastDone = 0; retransmit(s, o, &p, now); retransmitted = 1;
            }
        }
        s->rto = s->rto * 2 > C4F_SCTP_RTO_MAX ? C4F_SCTP_RTO_MAX : s->rto * 2;
        if (advanceForward(s) || tsnLt(s->peerCumTsn, s->forwardTsn)) addForwardTsn(s, &p);
        if (pktSend(s, &p)) return;
        s->t3 = anyOutstanding(s) ? now + s->rto : 0;
    }
    pump(s, now);
}

uint64_t c4fSctpDeadline(const C4fSctp *s)
{
    if (s->state != C4F_SCTP_ESTABLISHED) return 0;
    uint64_t due = s->t3;
    if (s->probeAt && (!due || s->probeAt < due)) due = s->probeAt;
    if (s->sendAt && (!due || s->sendAt < due)) due = s->sendAt;
    return due;
}
static void freeQueues(C4fSctp *s)
{
    while (s->out) { C4fSctpOut *o = s->out; s->out = o->next; free(o); }
    while (s->in) { C4fSctpIn *e = s->in; s->in = e->next; free(e); }
    s->outTail = NULL; s->outBytes = s->inBytes = 0; s->outChunks = 0;
    s->flightBytes = s->flightData = 0;
}

void c4fSctpInit(C4fSctp *s, const C4fSctpOps *ops)
{
    memset(s, 0, sizeof(*s));
    s->ops = *ops;
    s->state = C4F_SCTP_CLOSED;
    s->rto = C4F_SCTP_RTO_START;
}

void c4fSctpFree(C4fSctp *s)
{
    freeQueues(s);
    s->state = C4F_SCTP_ENDED;
}

static void onInit(C4fSctp *s, const uint8_t *packet, const uint8_t *v, size_t vlen)
{
    if (vlen < 16) return;
    uint32_t tag = get32(v), tsn = get32(v + 12);
    uint16_t os = get16(v + 8), mis = get16(v + 10);
    if (!tag || os < 2 || mis < 2 || get32(v + 4) < 1500) return;
    int duplicate = (s->state == C4F_SCTP_COOKIE_WAIT || s->state == C4F_SCTP_ESTABLISHED) &&
                    s->remoteTag == tag && s->remoteInitialTsn == tsn;
    if (!duplicate) {
        /* A restart needs a fresh DTLS/page session too, so ownership and
         * application sequence floors cannot survive an association reset. */
        if (s->state == C4F_SCTP_ESTABLISHED) { s->state = C4F_SCTP_ENDED; return; }
        freeQueues(s);
        memset(s->seen, 0, sizeof(s->seen));
        s->remotePort = get16(packet); s->localPort = get16(packet + 2);
        s->remoteTag = tag; s->remoteInitialTsn = tsn;
        do s->ops.random(s->ops.user, &s->localTag, sizeof(s->localTag)); while (!s->localTag);
        s->ops.random(s->ops.user, &s->nextTsn, sizeof(s->nextTsn));
        s->localInitialTsn = s->nextTsn;
        s->ops.random(s->ops.user, s->cookie, sizeof(s->cookie));
        s->cumTsn = s->highTsn = tsn - 1;
        s->nextSsn[0] = s->nextSsn[1] = s->ssn[0] = s->ssn[1] = 0;
        s->peerCumTsn = s->forwardTsn = s->nextTsn - 1;
        s->highestSent = s->nextTsn - 1;
        s->peerRwnd = get32(v + 4); s->cwnd = C4F_SCTP_INITIAL_CWND; s->ssthresh = s->peerRwnd;
        s->partialBytesAcked = 0; s->fastRecovery = 0; s->probeAt = s->sendAt = 0; s->timeouts = 0;
        s->rto = C4F_SCTP_RTO_START; s->rttKnown = 0; s->t3 = 0; s->sackDue = 0;
        s->state = C4F_SCTP_COOKIE_WAIT;
    }

    C4fPacket p;
    pktStart(s, &p, tag);
    uint8_t *a = pktChunk(&p, CHUNK_INIT_ACK, 0, 16 + 20 + 8 + 4);
    put32(a, s->localTag);
    put32(a + 4, C4F_SCTP_RWND);
    put16(a + 8, mis);                  /* outbound streams: as many as it takes in */
    put16(a + 10, 65535);               /* inbound */
    put32(a + 12, s->localInitialTsn);
    put16(a + 16, PARAM_STATE_COOKIE); put16(a + 18, 4 + sizeof(s->cookie));
    memcpy(a + 20, s->cookie, sizeof(s->cookie));
    put16(a + 36, PARAM_SUPPORTED_EXTENSIONS); put16(a + 38, 6);
    a[40] = CHUNK_FORWARD_TSN; a[41] = CHUNK_RECONFIG;   /* a[42..43]: padding */
    put16(a + 44, PARAM_FORWARD_TSN); put16(a + 46, 4);
    pktSend(s, &p);
}

static void onCookieEcho(C4fSctp *s, const uint8_t *v, size_t vlen)
{
    if (s->state != C4F_SCTP_COOKIE_WAIT && s->state != C4F_SCTP_ESTABLISHED) return;
    if (vlen != sizeof(s->cookie) || memcmp(v, s->cookie, sizeof(s->cookie))) return;
    s->state = C4F_SCTP_ESTABLISHED;
    C4fPacket p;
    pktStart(s, &p, s->remoteTag);
    pktChunk(&p, CHUNK_COOKIE_ACK, 0, 0);
    pktSend(s, &p);
}

/* Stream resets (RFC 6525): the browser closing a channel resets its side of
 * the stream; the next message on it starts again at 0. */
static void onReconfig(C4fSctp *s, C4fPacket *reply, const uint8_t *v, size_t vlen)
{
    for (size_t at = 0; at + 4 <= vlen; ) {
        uint16_t type = get16(v + at), size = get16(v + at + 2);
        if (size < 4 || at + size > vlen) break;
        if ((type == PARAM_OUTGOING_RESET && size >= 16) || (type == PARAM_INCOMING_RESET && size >= 8)) {
            uint32_t seq = get32(v + at + 4);
            int result = RECONFIG_DENIED;
            if (type == PARAM_OUTGOING_RESET) {
                size_t first = at + 16, count = (size - 16) / 2;
                if (!count) s->nextSsn[0] = s->nextSsn[1] = 0;
                for (size_t i = 0; i < count; i++) { uint16_t st = get16(v + first + 2 * i); if (st <= 1) s->nextSsn[st] = 0; }
                result = RECONFIG_PERFORMED;
            }
            uint8_t *r = pktChunk(reply, CHUNK_RECONFIG, 0, 12);
            if (!r) { pktSend(s, reply); r = pktChunk(reply, CHUNK_RECONFIG, 0, 12); }
            put16(r, PARAM_RECONFIG_RESPONSE); put16(r + 2, 12);
            put32(r + 4, seq); put32(r + 8, (uint32_t)result);
        }
        at += (size + 3) & ~3u;
    }
}

void c4fSctpReceive(C4fSctp *s, const uint8_t *packet, size_t len, uint64_t now)
{
    if (len < 16 || s->state == C4F_SCTP_ENDED) return;
    /* The checksum is taken over the packet with its own field as zero. */
    static const uint8_t zero[4];
    uint32_t c = c4fCrc32cUpdate(0xFFFFFFFFu, packet, 8);
    c = c4fCrc32cUpdate(c, zero, 4);
    c = c4fCrc32cUpdate(c, packet + 12, len - 12) ^ 0xFFFFFFFFu;
    uint32_t sent = (uint32_t)packet[8] | (uint32_t)packet[9] << 8 | (uint32_t)packet[10] << 16 | (uint32_t)packet[11] << 24;
    if (c != sent) { s->badChecksums++; return; }
    s->packetsIn++;
    uint32_t tag = get32(packet + 4);
    if (packet[12] == CHUNK_INIT) {
        size_t size = get16(packet + 14);
        if (tag == 0 && size >= 20 && size <= len - 12) onInit(s, packet, packet + 16, size - 4);
        return;
    }
    if (s->state == C4F_SCTP_CLOSED || tag != s->localTag) return;

    C4fPacket reply;
    pktStart(s, &reply, s->remoteTag);
    for (size_t at = 12; at + 4 <= len; ) {
        uint8_t type = packet[at], flags = packet[at + 1];
        size_t size = get16(packet + at + 2);
        if (size < 4 || at + size > len) break;
        const uint8_t *v = packet + at + 4;
        size_t vlen = size - 4;
        switch (type) {
        case CHUNK_DATA: onData(s, flags, v, vlen); break;
        case CHUNK_SACK: onSack(s, v, vlen, now); break;
        case CHUNK_HEARTBEAT: {
            uint8_t *r = pktChunk(&reply, CHUNK_HEARTBEAT_ACK, 0, vlen);
            if (!r) { pktSend(s, &reply); r = pktChunk(&reply, CHUNK_HEARTBEAT_ACK, 0, vlen); }
            if (r) memcpy(r, v, vlen);
            break;
        }
        case CHUNK_COOKIE_ECHO: onCookieEcho(s, v, vlen); break;
        case CHUNK_FORWARD_TSN: onForwardTsn(s, v, vlen); break;
        case CHUNK_RECONFIG: if (s->state == C4F_SCTP_ESTABLISHED) onReconfig(s, &reply, v, vlen); break;
        case CHUNK_SHUTDOWN:
            pktChunk(&reply, CHUNK_SHUTDOWN_ACK, 0, 0);
            pktSend(s, &reply);
            s->state = C4F_SCTP_ENDED;
            return;
        case CHUNK_ABORT: case CHUNK_SHUTDOWN_COMPLETE:
            s->state = C4F_SCTP_ENDED;
            return;
        case CHUNK_INIT_ACK: case CHUNK_COOKIE_ACK: case CHUNK_ERROR: case CHUNK_HEARTBEAT_ACK: case CHUNK_SHUTDOWN_ACK:
            break;
        default:
            /* Unknown: the top two bits say whether to skip it or stop here. */
            if (!(type & 0x80)) { at = len; continue; }
            break;
        }
        if (s->state == C4F_SCTP_ENDED) return;
        at += (size + 3) & ~(size_t)3;
    }
    if (s->sackDue && s->state == C4F_SCTP_ESTABLISHED && addSack(s, &reply) != 0) { pktSend(s, &reply); addSack(s, &reply); }
    pktSend(s, &reply);
}
