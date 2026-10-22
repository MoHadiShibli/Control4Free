/* STUN for an ICE-lite WebRTC endpoint: checks the browser's connectivity checks
 * and answers them. See c4f_stun.h. */
#include <string.h>

#include <mbedtls/md.h>

#include "c4f_stun.h"

enum {
    STUN_BINDING_REQUEST = 0x0001,
    STUN_BINDING_SUCCESS = 0x0101,
    ATTR_USERNAME = 0x0006,
    ATTR_MESSAGE_INTEGRITY = 0x0008,
    ATTR_XOR_MAPPED_ADDRESS = 0x0020,
    ATTR_USE_CANDIDATE = 0x0025,
    ATTR_FINGERPRINT = 0x8028,
};

static uint16_t c4fGet16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t c4fGet32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void c4fPut16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void c4fPut32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* CRC-32 in pieces: start from 0xFFFFFFFF, invert at the end. */
static uint32_t c4fCrc32Update(uint32_t c, const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        ready = 1;
    }
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c;
}

uint32_t c4fCrc32(const uint8_t *data, size_t len)
{
    return c4fCrc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/* A STUN message filling the whole datagram, or 0. */
static int c4fStunWellFormed(const uint8_t *msg, size_t len)
{
    if (len < 20 || len > 1500 || (msg[0] & 0xC0)) return 0;
    if (c4fGet32(msg + 4) != C4F_STUN_MAGIC) return 0;
    size_t body = c4fGet16(msg + 2);
    return (body & 3) == 0 && body + 20 == len;
}

/* Finds an attribute: the offset of its header, or 0. */
static size_t c4fStunFind(const uint8_t *msg, size_t len, uint16_t type)
{
    for (size_t at = 20; at + 4 <= len; ) {
        size_t size = c4fGet16(msg + at + 2);
        if (at + 4 + size > len) return 0;
        if (c4fGet16(msg + at) == type) return at;
        at += 4 + ((size + 3) & ~(size_t)3);
    }
    return 0;
}

/* HMAC-SHA1 over the message up to an attribute at `at`, with the header's
 * length set as if a MESSAGE-INTEGRITY there were the last attribute. */
static void c4fStunHmac(const uint8_t *msg, size_t at, const char *key, uint8_t out[20])
{
    uint8_t head[20];
    memcpy(head, msg, 20);
    c4fPut16(head + 2, (uint16_t)(at - 20 + 24));
    const mbedtls_md_info_t *sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, sha1, 1) == 0 &&
        mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key, strlen(key)) == 0 &&
        mbedtls_md_hmac_update(&ctx, head, 20) == 0 &&
        mbedtls_md_hmac_update(&ctx, msg + 20, at - 20) == 0 &&
        mbedtls_md_hmac_finish(&ctx, out) == 0) {
        mbedtls_md_free(&ctx);
        return;
    }
    mbedtls_md_free(&ctx);
    memset(out, 0, 20);
}

/* CRC-32 over the message up to a FINGERPRINT at `at`, the length counting it. */
static uint32_t c4fStunFingerprint(const uint8_t *msg, size_t at)
{
    uint8_t head[20];
    memcpy(head, msg, 20);
    c4fPut16(head + 2, (uint16_t)(at - 20 + 8));
    uint32_t c = c4fCrc32Update(0xFFFFFFFFu, head, 20);
    c = c4fCrc32Update(c, msg + 20, at - 20);
    return (c ^ 0xFFFFFFFFu) ^ 0x5354554Eu;
}

const uint8_t *c4fStunUsername(const uint8_t *msg, size_t len, size_t *size)
{
    if (!c4fStunWellFormed(msg, len) || c4fGet16(msg) != STUN_BINDING_REQUEST) return NULL;
    size_t at = c4fStunFind(msg, len, ATTR_USERNAME);
    if (!at) return NULL;
    *size = c4fGet16(msg + at + 2);
    return msg + at + 4;
}

int c4fStunVerify(const uint8_t *msg, size_t len, const char *key)
{
    if (!c4fStunWellFormed(msg, len)) return 0;
    size_t mi = c4fStunFind(msg, len, ATTR_MESSAGE_INTEGRITY);
    if (!mi || c4fGet16(msg + mi + 2) != 20) return 0;
    uint8_t mac[20];
    c4fStunHmac(msg, mi, key, mac);
    /* Constant time is not needed against a LAN guesser, but costs nothing. */
    uint8_t diff = 0;
    for (int i = 0; i < 20; i++) diff |= mac[i] ^ msg[mi + 4 + i];
    if (diff) return 0;
    /* Only FINGERPRINT may follow MESSAGE-INTEGRITY. */
    size_t after = mi + 24;
    if (after == len) return 1;
    if (after + 8 != len || c4fGet16(msg + after) != ATTR_FINGERPRINT || c4fGet16(msg + after + 2) != 4) return 0;
    return c4fGet32(msg + after + 4) == c4fStunFingerprint(msg, after);
}

size_t c4fStunAnswer(const uint8_t *msg, size_t len, const char *password,
                     uint32_t address, uint16_t port, uint8_t out[C4F_STUN_ANSWER_SIZE], int *nominate)
{
    *nominate = 0;
    if (!c4fStunWellFormed(msg, len) || c4fGet16(msg) != STUN_BINDING_REQUEST) return 0;
    if (!c4fStunVerify(msg, len, password)) return 0;
    size_t mi = c4fStunFind(msg, len, ATTR_MESSAGE_INTEGRITY);
    size_t use = c4fStunFind(msg, len, ATTR_USE_CANDIDATE);
    *nominate = use && use < mi;

    memset(out, 0, C4F_STUN_ANSWER_SIZE);
    c4fPut16(out, STUN_BINDING_SUCCESS);
    c4fPut16(out + 2, 44);                       /* 12 + 24 + 8 */
    memcpy(out + 4, msg + 4, 16);                /* magic cookie and transaction ID */
    /* XOR-MAPPED-ADDRESS: the address and port as the PS4 saw them. */
    uint8_t *a = out + 20;
    c4fPut16(a, ATTR_XOR_MAPPED_ADDRESS); c4fPut16(a + 2, 8);
    a[5] = 0x01;                                 /* IPv4 */
    uint16_t hostPort = (uint16_t)(((const uint8_t *)&port)[0] << 8 | ((const uint8_t *)&port)[1]);
    c4fPut16(a + 6, hostPort ^ (uint16_t)(C4F_STUN_MAGIC >> 16));
    uint32_t hostAddress = c4fGet32((const uint8_t *)&address);
    c4fPut32(a + 8, hostAddress ^ C4F_STUN_MAGIC);
    /* MESSAGE-INTEGRITY with our password, then FINGERPRINT. */
    uint8_t *m = out + 32;
    c4fPut16(m, ATTR_MESSAGE_INTEGRITY); c4fPut16(m + 2, 20);
    c4fStunHmac(out, 32, password, m + 4);
    uint8_t *f = out + 56;
    c4fPut16(f, ATTR_FINGERPRINT); c4fPut16(f + 2, 4);
    c4fPut32(f + 4, c4fStunFingerprint(out, 56));
    return C4F_STUN_ANSWER_SIZE;
}
