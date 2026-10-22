/* STUN and SCTP, as the WebRTC transport uses them, on the host. STUN against the
 * RFC 5769 test vectors; SCTP driven packet by packet the way a browser would. */
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c4f_sctp.h"
#include "c4f_stun.h"

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

/* ------------------------------------------------------------ STUN */

static const uint8_t rfcRequest[] = {
    0x00,0x01,0x00,0x58, 0x21,0x12,0xa4,0x42, 0xb7,0xe7,0xa7,0x01, 0xbc,0x34,0xd6,0x86, 0xfa,0x87,0xdf,0xae,
    0x80,0x22,0x00,0x10, 0x53,0x54,0x55,0x4e, 0x20,0x74,0x65,0x73, 0x74,0x20,0x63,0x6c, 0x69,0x65,0x6e,0x74,
    0x00,0x24,0x00,0x04, 0x6e,0x00,0x01,0xff,
    0x80,0x29,0x00,0x08, 0x93,0x2f,0xf9,0xb1, 0x51,0x26,0x3b,0x36,
    0x00,0x06,0x00,0x09, 0x65,0x76,0x74,0x6a, 0x3a,0x68,0x36,0x76, 0x59,0x20,0x20,0x20,
    0x00,0x08,0x00,0x14, 0x9a,0xea,0xa7,0x0c, 0xbf,0xd8,0xcb,0x56, 0x78,0x1e,0xf2,0xb5, 0xb2,0xd3,0xf2,0x49, 0xc1,0xb5,0x71,0xa2,
    0x80,0x28,0x00,0x04, 0xe5,0x7a,0x3b,0xcf,
};
static const uint8_t rfcResponse[] = {
    0x01,0x01,0x00,0x3c, 0x21,0x12,0xa4,0x42, 0xb7,0xe7,0xa7,0x01, 0xbc,0x34,0xd6,0x86, 0xfa,0x87,0xdf,0xae,
    0x80,0x22,0x00,0x0b, 0x74,0x65,0x73,0x74, 0x20,0x76,0x65,0x63, 0x74,0x6f,0x72,0x20,
    0x00,0x20,0x00,0x08, 0x00,0x01,0xa1,0x47, 0xe1,0x12,0xa6,0x43,
    0x00,0x08,0x00,0x14, 0x2b,0x91,0xf5,0x99, 0xfd,0x9e,0x90,0xc3, 0x8c,0x74,0x89,0xf9, 0x2a,0xf9,0xba,0x53, 0xf0,0x6b,0xe7,0xd7,
    0x80,0x28,0x00,0x04, 0xc0,0x7d,0x4c,0x96,
};
static const char rfcPassword[] = "VOkJxbRl1RmTxUk/WvJxBt";

static void testStun(void)
{
    CHECK(c4fCrc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(c4fCrc32c((const uint8_t *)"123456789", 9) == 0xE3069283u);
    CHECK(c4fIsStun(rfcRequest, sizeof(rfcRequest)) && !c4fIsDtls(rfcRequest, sizeof(rfcRequest)));
    size_t size;
    const uint8_t *user = c4fStunUsername(rfcRequest, sizeof(rfcRequest), &size);
    CHECK(user && size == 9 && !memcmp(user, "evtj:h6vY", 9));
    CHECK(c4fStunVerify(rfcRequest, sizeof(rfcRequest), rfcPassword));
    CHECK(!c4fStunVerify(rfcRequest, sizeof(rfcRequest), "wrong password"));
    CHECK(c4fStunVerify(rfcResponse, sizeof(rfcResponse), rfcPassword));
    /* A changed byte breaks the integrity check. */
    uint8_t bent[sizeof(rfcRequest)];
    memcpy(bent, rfcRequest, sizeof(bent));
    bent[47] ^= 1;
    CHECK(!c4fStunVerify(bent, sizeof(bent), rfcPassword));
    CHECK(!c4fStunUsername(rfcResponse, sizeof(rfcResponse), &size));   /* a response, not a request */

    /* Our answer to the RFC's request, from 192.0.2.1:32853, carries the same
     * XOR-MAPPED-ADDRESS as the RFC's sample response and checks out. */
    uint8_t answer[C4F_STUN_ANSWER_SIZE];
    int nominate = -1;
    uint32_t address = inet_addr("192.0.2.1");
    size_t n = c4fStunAnswer(rfcRequest, sizeof(rfcRequest), rfcPassword, address, htons(32853), answer, &nominate);
    CHECK(n == C4F_STUN_ANSWER_SIZE && nominate == 0);
    CHECK(answer[0] == 0x01 && answer[1] == 0x01 && !memcmp(answer + 4, rfcRequest + 4, 16));
    CHECK(!memcmp(answer + 20, rfcResponse + 36, 12));
    CHECK(c4fStunVerify(answer, n, rfcPassword));
    CHECK(!c4fStunAnswer(rfcRequest, sizeof(rfcRequest), "another", address, htons(32853), answer, &nominate));
    puts("PASS stun: RFC 5769 request and response, integrity, fingerprint, our XOR-MAPPED-ADDRESS answer");
}

/* ------------------------------------------------------------ SCTP */

typedef struct { uint8_t data[2048]; size_t len; } Blob;
static Blob sent[512], delivered[64];
static uint16_t deliveredStream[64];
static int sentCount, deliveredCount;
static int rejectSend;
static uint32_t randomState = 0x12345678;

static int opSend(void *user, const uint8_t *p, size_t len)
{
    (void)user;
    if (rejectSend) return -1;
    CHECK(sentCount < 512 && len <= C4F_SCTP_MTU);
    /* Every packet we send carries a correct CRC32c, least significant byte first. */
    uint8_t copy[2048];
    memcpy(copy, p, len);
    memset(copy + 8, 0, 4);
    uint32_t crc = c4fCrc32c(copy, len);
    CHECK(p[8] == (uint8_t)crc && p[9] == (uint8_t)(crc >> 8) && p[10] == (uint8_t)(crc >> 16) && p[11] == (uint8_t)(crc >> 24));
    memcpy(sent[sentCount].data, p, len); sent[sentCount++].len = len;
    return 0;
}

static void opDeliver(void *user, uint16_t stream, uint32_t ppid, const uint8_t *d, size_t len)
{
    (void)user;
    CHECK(deliveredCount < 64 && ppid == C4F_SCTP_PPID_STRING);
    memcpy(delivered[deliveredCount].data, d, len); delivered[deliveredCount].len = len;
    deliveredStream[deliveredCount++] = stream;
}

static void opRandom(void *user, void *out, size_t len)
{
    (void)user;
    for (size_t i = 0; i < len; i++) { randomState = randomState * 1103515245u + 12345u; ((uint8_t *)out)[i] = (uint8_t)(randomState >> 16); }
}

static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void p16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* A browser-side packet under construction. */
typedef struct { uint8_t b[2048]; size_t len; } Pkt;
static void pkStart(Pkt *k, uint32_t tag) { memset(k, 0, sizeof(*k)); p16(k->b, 5000); p16(k->b + 2, 5000); p32(k->b + 4, tag); k->len = 12; }
static uint8_t *pkChunk(Pkt *k, uint8_t type, uint8_t flags, size_t vlen)
{
    uint8_t *c = k->b + k->len;
    c[0] = type; c[1] = flags; p16(c + 2, (uint16_t)(4 + vlen));
    k->len += (4 + vlen + 3) & ~(size_t)3;
    return c + 4;
}
static void pkData(Pkt *k, uint8_t flags, uint32_t tsn, uint16_t stream, uint16_t ssn, const char *text)
{
    size_t n = strlen(text);
    uint8_t *v = pkChunk(k, 0, flags, 12 + n);
    p32(v, tsn); p16(v + 4, stream); p16(v + 6, ssn); p32(v + 8, C4F_SCTP_PPID_STRING); memcpy(v + 12, text, n);
}
static void pkSend(C4fSctp *s, Pkt *k, uint64_t now)
{
    uint32_t crc = c4fCrc32c(k->b, k->len);
    k->b[8] = (uint8_t)crc; k->b[9] = (uint8_t)(crc >> 8); k->b[10] = (uint8_t)(crc >> 16); k->b[11] = (uint8_t)(crc >> 24);
    c4fSctpReceive(s, k->b, k->len, now);
}

/* The first chunk of type `type` in a sent packet, or NULL. */
static const uint8_t *findChunk(const Blob *b, uint8_t type)
{
    for (size_t at = 12; at + 4 <= b->len; at += (g16(b->data + at + 2) + 3) & ~3u)
        if (b->data[at] == type) return b->data + at;
    return NULL;
}

static int isDelivered(int i, uint16_t stream, const char *text)
{
    return i < deliveredCount && deliveredStream[i] == stream && delivered[i].len == strlen(text) &&
           !memcmp(delivered[i].data, text, delivered[i].len);
}

static void testSctp(void)
{
    C4fSctp s;
    C4fSctpOps ops = { NULL, opSend, opDeliver, opRandom };
    c4fSctpInit(&s, &ops);
    uint64_t now = 1000;
    const uint32_t browserTag = 0x11223344, browserTsn = 1000;

    /* INIT -> INIT-ACK with a cookie, partial reliability and stream resets. */
    Pkt k;
    pkStart(&k, 0);
    uint8_t *v = pkChunk(&k, 1, 0, 16);
    p32(v, browserTag); p32(v + 4, 131072); p16(v + 8, 65535); p16(v + 10, 65535); p32(v + 12, browserTsn);
    pkSend(&s, &k, now);
    CHECK(sentCount == 1 && g32(sent[0].data + 4) == browserTag);
    const uint8_t *ack = findChunk(&sent[0], 2);
    CHECK(ack && s.state == C4F_SCTP_COOKIE_WAIT);
    uint32_t ourTag = g32(ack + 4), ourTsn = g32(ack + 16);
    CHECK(ourTag == s.localTag && g16(ack + 20) == 7 && g16(ack + 22) == 20);
    uint8_t cookie[16];
    memcpy(cookie, ack + 24, 16);
    CHECK(g16(ack + 40) == 0x8008 && ack[44] == 0xC0 && ack[45] == 0x82 && g16(ack + 48) == 0xC000);

    /* A wrong cookie is ignored; the right one establishes. */
    pkStart(&k, ourTag); v = pkChunk(&k, 10, 0, 16); memset(v, 0xAA, 16); pkSend(&s, &k, now);
    CHECK(s.state == C4F_SCTP_COOKIE_WAIT && sentCount == 1);
    pkStart(&k, ourTag); v = pkChunk(&k, 10, 0, 16); memcpy(v, cookie, 16); pkSend(&s, &k, now);
    CHECK(s.state == C4F_SCTP_ESTABLISHED && sentCount == 2 && findChunk(&sent[1], 11));

    /* A packet under another tag, or with a bad checksum, is dropped. */
    pkStart(&k, 0x99999999); pkData(&k, 3, browserTsn, 0, 0, "nope"); pkSend(&s, &k, now);
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn, 0, 0, "nope"); k.b[k.len - 1] ^= 0x55;
    c4fSctpReceive(&s, k.b, k.len, now);
    CHECK(deliveredCount == 0 && s.badChecksums == 1);

    /* In order on stream 0, then a gap, then unordered on stream 1 delivered at once. */
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn, 0, 0, "hello"); pkSend(&s, &k, now);
    CHECK(isDelivered(0, 0, "hello") && s.cumTsn == browserTsn);
    const uint8_t *sack = findChunk(&sent[sentCount - 1], 3);
    CHECK(sack && g32(sack + 4) == browserTsn && g16(sack + 12) == 0);
    pkStart(&k, ourTag); pkData(&k, 7, browserTsn + 2, 1, 0, "fast"); pkSend(&s, &k, now);
    CHECK(isDelivered(1, 1, "fast") && s.cumTsn == browserTsn);
    sack = findChunk(&sent[sentCount - 1], 3);
    CHECK(g32(sack + 4) == browserTsn && g16(sack + 12) == 1 && g16(sack + 16) == 2 && g16(sack + 18) == 2);
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn + 1, 0, 1, "second"); pkSend(&s, &k, now);
    CHECK(isDelivered(2, 0, "second") && s.cumTsn == browserTsn + 2);
    /* The same TSN again: acknowledged, not delivered twice. */
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn + 1, 0, 1, "second"); pkSend(&s, &k, now);
    CHECK(deliveredCount == 3 && s.duplicates == 1);

    /* Out of order on the ordered stream: held until its turn. */
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn + 4, 0, 3, "four"); pkSend(&s, &k, now);
    CHECK(deliveredCount == 3);
    pkStart(&k, ourTag); pkData(&k, 3, browserTsn + 3, 0, 2, "three"); pkSend(&s, &k, now);
    CHECK(isDelivered(3, 0, "three") && isDelivered(4, 0, "four") && s.cumTsn == browserTsn + 4);

    /* A message in three pieces, arriving last, first, middle. */
    pkStart(&k, ourTag); pkData(&k, 1, browserTsn + 7, 0, 4, "ghi"); pkSend(&s, &k, now);
    pkStart(&k, ourTag); pkData(&k, 2, browserTsn + 5, 0, 4, "abc"); pkSend(&s, &k, now);
    CHECK(deliveredCount == 5);
    pkStart(&k, ourTag); pkData(&k, 0, browserTsn + 6, 0, 4, "def"); pkSend(&s, &k, now);
    CHECK(isDelivered(5, 0, "abcdefghi") && s.inBytes == 0);

    /* FORWARD-TSN skips an unordered piece that will never be finished. */
    pkStart(&k, ourTag); pkData(&k, 6, browserTsn + 8, 1, 0, "lost-fir"); pkSend(&s, &k, now);
    pkStart(&k, ourTag); v = pkChunk(&k, 192, 0, 4); p32(v, browserTsn + 9); pkSend(&s, &k, now);
    CHECK(s.cumTsn == browserTsn + 9 && s.inBytes == 0 && deliveredCount == 6);
    pkStart(&k, ourTag); pkData(&k, 7, browserTsn + 10, 1, 0, "after"); pkSend(&s, &k, now);
    CHECK(isDelivered(6, 1, "after"));

    /* Sending: reliable and in order on stream 0, acknowledged by a SACK. */
    int before = sentCount;
    CHECK(c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"reply", 5, now) == 0);
    CHECK(sentCount == before + 1);
    const uint8_t *data = findChunk(&sent[before], 0);
    CHECK(data && data[1] == 3 && g32(data + 4) == ourTsn && g16(data + 8) == 0 && g16(data + 10) == 0 &&
          g32(data + 12) == C4F_SCTP_PPID_STRING && !memcmp(data + 16, "reply", 5));
    CHECK(c4fSctpDeadline(&s) == now + 200);
    pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 12); p32(v, ourTsn); p32(v + 4, 131072); pkSend(&s, &k, now + 5);
    CHECK(s.out == NULL && c4fSctpDeadline(&s) == 0);

    /* Not acknowledged: resent, same TSN, after the timeout. */
    now += 1000;
    before = sentCount;
    CHECK(c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"again", 5, now) == 0);
    uint64_t due = c4fSctpDeadline(&s);
    CHECK(due > now && due <= now + 200);
    c4fSctpTick(&s, due - 1);
    CHECK(sentCount == before + 1);
    c4fSctpTick(&s, due);
    CHECK(sentCount == before + 2 && s.resent == 1);
    data = findChunk(&sent[before + 1], 0);
    CHECK(data && g32(data + 4) == ourTsn + 1 && !memcmp(data + 16, "again", 5));
    pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 12); p32(v, ourTsn + 1); p32(v + 4, 131072); pkSend(&s, &k, due + 1);
    CHECK(s.out == NULL);

    /* Unordered on stream 1 is never resent: given up on, then skipped with FORWARD-TSN. */
    now += 1000;
    before = sentCount;
    CHECK(c4fSctpSend(&s, 1, C4F_SCTP_PPID_STRING, (const uint8_t *)"rumble", 6, now) == 0);
    data = findChunk(&sent[before], 0);
    CHECK(data && data[1] == 7);
    due = c4fSctpDeadline(&s);
    c4fSctpTick(&s, due);
    const uint8_t *fwd = findChunk(&sent[sentCount - 1], 192);
    CHECK(s.abandoned == 1 && fwd && g32(fwd + 4) == ourTsn + 2 && !findChunk(&sent[sentCount - 1], 0));
    pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 12); p32(v, ourTsn + 2); p32(v + 4, 131072); pkSend(&s, &k, due + 1);
    CHECK(s.out == NULL && c4fSctpDeadline(&s) == 0);

    /* Three newly acknowledged later TSNs trigger fast retransmit. Repeated
     * copies of an old SACK do not manufacture more evidence of a hole. */
    now += 1000;
    c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"A", 1, now);
    c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"B", 1, now);
    c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"C", 1, now);
    c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)"D", 1, now);
    before = sentCount;
    for (int i = 0; i < 3; i++) {
        pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 16); p32(v, ourTsn + 2); p32(v + 4, 131072); p16(v + 8, 1); p16(v + 12, 2); p16(v + 14, 2);
        pkSend(&s, &k, now + 10);
    }
    CHECK(s.resent == 1 && sentCount == before);
    for (int i = 3; i <= 4; i++) {
        pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 16); p32(v, ourTsn + 2); p32(v + 4, 131072);
        p16(v + 8, 1); p16(v + 12, 2); p16(v + 14, (uint16_t)i); pkSend(&s, &k, now + 10);
    }
    CHECK(s.resent == 2 && sentCount == before + 1);
    data = findChunk(&sent[before], 0);
    CHECK(data && g32(data + 4) == ourTsn + 3 && data[16] == 'A');
    pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 12); p32(v, ourTsn + 6); p32(v + 4, 131072); pkSend(&s, &k, now + 20);
    CHECK(s.out == NULL);

    /* A message bigger than a packet goes in pieces, the same SSN on each. */
    char big[3000];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (char)('a' + i % 26);
    before = sentCount;
    CHECK(c4fSctpSend(&s, 0, C4F_SCTP_PPID_STRING, (const uint8_t *)big, sizeof(big), now) == 0);
    CHECK(sentCount == before + 3);
    size_t total = 0;
    for (int i = 0; i < 3; i++) {
        data = findChunk(&sent[before + i], 0);
        CHECK(data && g16(data + 10) == 6);
        CHECK((data[1] & 3) == (i == 0 ? 2 : i == 2 ? 1 : 0));
        total += g16(data + 2) - 16u;
    }
    CHECK(total == sizeof(big));
    pkStart(&k, ourTag); v = pkChunk(&k, 3, 0, 12); p32(v, ourTsn + 9); p32(v + 4, 131072); pkSend(&s, &k, now + 30);
    CHECK(s.out == NULL);

    /* HEARTBEAT is echoed; a stream reset is performed and answered. */
    pkStart(&k, ourTag); v = pkChunk(&k, 4, 0, 8); p16(v, 1); p16(v + 2, 8); memcpy(v + 4, "beat", 4); pkSend(&s, &k, now);
    const uint8_t *hb = findChunk(&sent[sentCount - 1], 5);
    CHECK(hb && !memcmp(hb + 8, "beat", 4));
    pkStart(&k, ourTag); v = pkChunk(&k, 130, 0, 18);
    p16(v, 13); p16(v + 2, 18); p32(v + 4, 777); p32(v + 8, 0); p32(v + 12, browserTsn + 10); p16(v + 16, 0);
    pkSend(&s, &k, now);
    const uint8_t *rc = findChunk(&sent[sentCount - 1], 130);
    CHECK(rc && g16(rc + 4) == 16 && g32(rc + 8) == 777 && g32(rc + 12) == 1 && s.nextSsn[0] == 0);

    /* ABORT ends it. */
    pkStart(&k, ourTag); pkChunk(&k, 6, 0, 0); pkSend(&s, &k, now);
    CHECK(s.state == C4F_SCTP_ENDED && c4fSctpSend(&s, 0, 51, (const uint8_t *)"x", 1, now) == -1);
    c4fSctpFree(&s);
    puts("PASS sctp: INIT/cookie, tags, checksums, ordering, gaps, reassembly, FORWARD-TSN, resend, give-up, fast resend, fragments, heartbeat, reset, abort");
}

static void openAssociation(C4fSctp *s, uint64_t now)
{
    C4fSctpOps ops = { NULL, opSend, opDeliver, opRandom };
    sentCount = deliveredCount = rejectSend = 0;
    c4fSctpInit(s, &ops);
    Pkt k;
    pkStart(&k, 0);
    uint8_t *v = pkChunk(&k, 1, 0, 16);
    p32(v, 0xabcdef01); p32(v + 4, 131072); p16(v + 8, 2); p16(v + 10, 2); p32(v + 12, 1000);
    pkSend(s, &k, now);
    pkStart(&k, s->localTag); v = pkChunk(&k, 10, 0, 16); memcpy(v, s->cookie, 16); pkSend(s, &k, now);
    CHECK(s->state == C4F_SCTP_ESTABLISHED);
    sentCount = 0;
}

static void acknowledge(C4fSctp *s, uint32_t cum, uint32_t window, uint64_t now)
{
    Pkt k;
    pkStart(&k, s->localTag);
    uint8_t *v = pkChunk(&k, 3, 0, 12);
    p32(v, cum); p32(v + 4, window); pkSend(s, &k, now);
}

static void testWindows(void)
{
    C4fSctp s;
    uint64_t now = 1000;
    openAssociation(&s, now);
    uint32_t first = s.nextTsn;
    char big[C4F_SCTP_MAX_MESSAGE];
    memset(big, 'q', sizeof(big));
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)big, sizeof(big), now) == 0);
    CHECK(sentCount > 0 && sentCount <= 4 && s.flightBytes <= s.cwnd && s.outBytes == sizeof(big));
    CHECK(s.highestSent < s.nextTsn - 1); /* remaining fragments queued, not falsely sent */
    uint32_t beforeSeq = s.nextTsn;
    CHECK(c4fSctpSend(&s, 1, 51, (const uint8_t *)"stale", 5, now) == -1 && s.nextTsn == beforeSeq);
    int before = sentCount;
    acknowledge(&s, s.nextTsn - 1, 131072, now + 1); /* cannot ACK unsent queued data */
    CHECK(sentCount == before && s.peerCumTsn == first - 1);
    size_t bytes = 0;
    for (unsigned tries = 0; s.out && tries < 32; tries++) {
        acknowledge(&s, s.highestSent, 131072, ++now);
        CHECK(s.flightBytes <= s.cwnd);
    }
    CHECK(!s.out && s.outBytes == 0 && s.flightBytes == 0 && !c4fSctpDeadline(&s));
    for (int i = 0; i < sentCount; i++) {
        const uint8_t *data = findChunk(&sent[i], 0);
        CHECK(data); bytes += g16(data + 2) - 16u;
    }
    CHECK(bytes == sizeof(big));
    c4fSctpFree(&s);

    /* A large already-open window still emits a bounded burst, but schedules
     * its continuation promptly instead of waiting for the retransmit timer. */
    openAssociation(&s, now);
    s.cwnd = 30000;
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)big, sizeof(big), now) == 0 && sentCount == 4);
    CHECK(c4fSctpDeadline(&s) == now + 1);
    c4fSctpTick(&s, now + 1); CHECK(sentCount == 8);
    c4fSctpFree(&s);

    /* Receive-window closure buffers reliable control and rejects replaceable
     * state. A timed probe prevents a lost window update from deadlocking it. */
    openAssociation(&s, now);
    acknowledge(&s, s.highestSent, 0, now);
    first = s.nextTsn;
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)big, 2000, now) == 0 && sentCount == 0);
    CHECK(c4fSctpSend(&s, 1, 51, (const uint8_t *)"stale", 5, now) == -1);
    uint64_t due = c4fSctpDeadline(&s);
    CHECK(due == now + s.rto);
    c4fSctpTick(&s, due - 1); CHECK(sentCount == 0);
    c4fSctpTick(&s, due); CHECK(sentCount == 1 && s.highestSent == first);
    acknowledge(&s, first - 1, 0, due + 1); CHECK(sentCount == 1);
    acknowledge(&s, first, 131072, due + 2); CHECK(sentCount == 2);
    acknowledge(&s, s.highestSent, 131072, due + 3); CHECK(!s.out && !s.flightBytes);

    /* A too-small positive receive window is also respected before the probe. */
    acknowledge(&s, s.highestSent, 20, due + 4);
    before = sentCount;
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)big, 500, due + 4) == 0 && sentCount == before);
    acknowledge(&s, s.highestSent, 131072, due + 5); CHECK(sentCount == before + 1);
    acknowledge(&s, s.highestSent, 131072, due + 6);
    c4fSctpFree(&s);

    openAssociation(&s, now);
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)big, 3000, now) == 0);
    before = sentCount; due = c4fSctpDeadline(&s);
    c4fSctpTick(&s, due);
    CHECK(s.cwnd == C4F_SCTP_MTU && sentCount == before + 1 && s.resent == 1);
    c4fSctpFree(&s);

    /* The output callback cannot silently reject reliable DATA. */
    openAssociation(&s, now);
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)"keep", 4, now) == 0);
    uint32_t savedTag = s.localTag, savedNext = s.nextTsn;
    size_t savedBytes = s.outBytes;
    Pkt retry;
    pkStart(&retry, 0);
    uint8_t *init = pkChunk(&retry, 1, 0, 16);
    p32(init, 0xabcdef01); p32(init + 4, 131072); p16(init + 8, 2); p16(init + 10, 2); p32(init + 12, 1000);
    pkSend(&s, &retry, now + 1);
    const uint8_t *initAck = findChunk(&sent[sentCount - 1], 2);
    CHECK(initAck && s.state == C4F_SCTP_ESTABLISHED && s.localTag == savedTag && s.nextTsn == savedNext &&
          s.outBytes == savedBytes && g32(initAck + 16) == s.localInitialTsn);
    /* A declared chunk beyond the datagram boundary cannot reset the peer. */
    pkStart(&retry, 0); init = pkChunk(&retry, 1, 0, 16); p16(init - 2, 65535);
    pkSend(&s, &retry, now + 2);
    CHECK(s.state == C4F_SCTP_ESTABLISHED && s.localTag == savedTag && s.outBytes == savedBytes);
    rejectSend = 1;
    CHECK(c4fSctpSend(&s, 0, 51, (const uint8_t *)"fatal", 5, now) == -1 && s.state == C4F_SCTP_ENDED);
    rejectSend = 0;
    c4fSctpFree(&s);
    puts("PASS sctp windows: bounded congestion, queued fragments, receive credit/probes, stale-send rejection, timeout burst and send failures");
}

int main(void)
{
    testStun();
    testSctp();
    testWindows();
    return 0;
}
