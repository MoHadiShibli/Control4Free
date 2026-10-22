/* WebRTC for the controller page: ICE-lite, DTLS through Mbed TLS, and the
 * SCTP in sctp.c. See c4f_rtc.h. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "c4f_log.h"
#include "c4f_rtc.h"
#include "c4f_sctp.h"
#include "c4f_stun.h"

#define C4F_RTC_MTU 1200
#define C4F_RTC_TEXT 4096         /* the biggest message the service takes (C4F_NET_MESSAGE) */
#define C4F_RTC_OUT_BYTES (64u * 1024u)
#define C4F_RTC_OUT_PACKETS 64u
#define C4F_RTC_WRITE_BURST 8u

typedef struct C4fRtcDatagram C4fRtcDatagram;
struct C4fRtcDatagram {
    C4fRtcDatagram *next;
    struct sockaddr_in to;
    size_t len;
    uint8_t data[];
};

struct C4fRtcCrypto {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509_crt cert;
    mbedtls_ssl_config conf;
};

struct C4fRtcPeer {
    C4fConn conn;                  /* first: the service holds a peer by it */
    C4fRtc *rtc;
    unsigned id;
    char localUfrag[9], localPwd[25], remoteUfrag[257];
    uint8_t remoteFingerprint[32];
    struct sockaddr_in addr;
    int addrKnown;
    mbedtls_ssl_context ssl;
    int dtlsUp;
    const uint8_t *in;             /* the datagram Mbed TLS is reading, until it has */
    size_t inLen;
    uint64_t timerAt;
    uint32_t timerInt, timerFin;
    C4fSctp sctp;
    int opened, ending, failed;
    const char *failure;
    uint64_t createdAt, heardAt;
    uint64_t datagramsIn, datagramsOut;
    C4fRtcDatagram *out, *outTail;
    size_t outBytes;
    unsigned outPackets;
    uint64_t retryWriteAt;
};

/* ------------------------------------------------------------ certificate */

static int c4fRtcEntropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    arc4random_buf(out, len);
    *olen = len;
    return 0;
}

static void c4fRtcFreeCrypto(C4fRtcCrypto *k)
{
    mbedtls_ssl_config_free(&k->conf);
    mbedtls_x509_crt_free(&k->cert);
    mbedtls_pk_free(&k->key);
    mbedtls_ctr_drbg_free(&k->drbg);
    mbedtls_entropy_free(&k->entropy);
    free(k);
}

/* A fresh P-256 key and a self-signed certificate for it, once per run: the
 * browser checks it by the fingerprint in the answer, not by who signed it. */
static int c4fRtcMakeCrypto(C4fRtc *rtc)
{
    C4fRtcCrypto *k = calloc(1, sizeof(*k));
    if (!k) return -1;
    uint64_t started = c4fTimeMs();
    mbedtls_entropy_init(&k->entropy);
    mbedtls_ctr_drbg_init(&k->drbg);
    mbedtls_pk_init(&k->key);
    mbedtls_x509_crt_init(&k->cert);
    mbedtls_ssl_config_init(&k->conf);
    unsigned char der[1024];
    int derLen = 0;
    int ret = mbedtls_entropy_add_source(&k->entropy, c4fRtcEntropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (!ret) ret = mbedtls_ctr_drbg_seed(&k->drbg, mbedtls_entropy_func, &k->entropy, (const unsigned char *)"Control4Free", 12);
    if (!ret) ret = mbedtls_pk_setup(&k->key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (!ret) ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(k->key), mbedtls_ctr_drbg_random, &k->drbg);
    if (!ret) {
        mbedtls_x509write_cert w;
        unsigned char serial[8];
        mbedtls_x509write_crt_init(&w);
        mbedtls_x509write_crt_set_version(&w, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&w, &k->key);
        mbedtls_x509write_crt_set_issuer_key(&w, &k->key);
        ret = mbedtls_x509write_crt_set_subject_name(&w, "CN=Control4Free");
        if (!ret) ret = mbedtls_x509write_crt_set_issuer_name(&w, "CN=Control4Free");
        if (!ret) ret = mbedtls_ctr_drbg_random(&k->drbg, serial, sizeof(serial));
        serial[0] = (serial[0] & 0x7F) | 0x01;   /* positive, no leading zero */
        if (!ret) ret = mbedtls_x509write_crt_set_serial_raw(&w, serial, sizeof(serial));
        if (!ret) ret = mbedtls_x509write_crt_set_validity(&w, "20250101000000", "20450101000000");
        if (!ret) {
            derLen = mbedtls_x509write_crt_der(&w, der, sizeof(der), mbedtls_ctr_drbg_random, &k->drbg);
            if (derLen < 0) ret = derLen;
        }
        mbedtls_x509write_crt_free(&w);
    }
    const unsigned char *cert = der + sizeof(der) - derLen;   /* written at the end of the buffer */
    if (!ret) ret = mbedtls_x509_crt_parse_der(&k->cert, cert, (size_t)derLen);
    if (!ret) {
        unsigned char hash[32];
        ret = mbedtls_sha256(cert, (size_t)derLen, hash, 0);
        for (int i = 0; !ret && i < 32; i++) snprintf(rtc->fingerprint + 3 * i, 4, i < 31 ? "%02X:" : "%02X", hash[i]);
    }
    if (!ret) ret = mbedtls_ssl_config_defaults(&k->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                                MBEDTLS_SSL_PRESET_DEFAULT);
    if (!ret) {
        mbedtls_ssl_conf_rng(&k->conf, mbedtls_ctr_drbg_random, &k->drbg);
        /* The browser's certificate is self-signed too: asked for, then checked
         * against the fingerprint in its offer. */
        mbedtls_ssl_conf_authmode(&k->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_handshake_timeout(&k->conf, 400, 6000);
        ret = mbedtls_ssl_conf_own_cert(&k->conf, &k->cert, &k->key);
    }
    if (ret) {
        c4fLog("rtc: making the certificate failed: -0x%04x\n", (unsigned)-ret);
        c4fRtcFreeCrypto(k);
        return -1;
    }
    rtc->crypto = k;
    c4fLog("rtc: certificate ready in %llu ms\n", (unsigned long long)(c4fTimeMs() - started));
    return 0;
}

/* ------------------------------------------------------------ peers */

static int c4fRtcPeerTimer(void *ctx)
{
    C4fRtcPeer *p = ctx;
    if (!p->timerFin) return -1;
    uint64_t elapsed = c4fTimeMs() - p->timerAt;
    return elapsed >= p->timerFin ? 2 : elapsed >= p->timerInt ? 1 : 0;
}

static void c4fRtcPeerSetTimer(void *ctx, uint32_t intMs, uint32_t finMs)
{
    C4fRtcPeer *p = ctx;
    p->timerAt = c4fTimeMs();
    p->timerInt = intMs;
    p->timerFin = finMs;
}

static int c4fRtcSendDatagram(C4fRtcPeer *p, const unsigned char *buf, size_t len, const struct sockaddr_in *to)
{
    if (p->failed || len > C4F_RTC_MTU) return -1;
    if (!p->out) {
        ssize_t n = sendto(p->rtc->fd, buf, len, 0, (const struct sockaddr *)to, sizeof(*to));
        if (n == (ssize_t)len) { p->datagramsOut++; return 0; }
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ENOBUFS)) {
            p->failed = 1; p->failure = "UDP send failed"; return -1;
        }
        if (errno == ENOBUFS) p->retryWriteAt = c4fTimeMs() + 2;
    }
    if (p->outPackets >= C4F_RTC_OUT_PACKETS || p->outBytes + len > C4F_RTC_OUT_BYTES) {
        p->failed = 1; p->failure = "UDP output queue full"; return -1;
    }
    C4fRtcDatagram *d = malloc(sizeof(*d) + len);
    if (!d) { p->failed = 1; p->failure = "UDP output allocation failed"; return -1; }
    d->next = NULL; d->to = *to; d->len = len; memcpy(d->data, buf, len);
    if (p->outTail) p->outTail->next = d; else p->out = d;
    p->outTail = d; p->outBytes += len; p->outPackets++;
    return 0;
}

static void c4fRtcDrainPeer(C4fRtcPeer *p, uint64_t now)
{
    if (p->ending || p->failed || now < p->retryWriteAt) return;
    for (unsigned i = 0; p->out && i < C4F_RTC_WRITE_BURST; i++) {
        C4fRtcDatagram *d = p->out;
        ssize_t n = sendto(p->rtc->fd, d->data, d->len, 0, (const struct sockaddr *)&d->to, sizeof(d->to));
        if (n != (ssize_t)d->len) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENOBUFS)) {
                if (errno == ENOBUFS) p->retryWriteAt = now + 2;
            } else { p->failed = 1; p->failure = "UDP queued send failed"; }
            return;
        }
        p->datagramsOut++; p->out = d->next;
        p->outBytes -= d->len; p->outPackets--; free(d);
        if (!p->out) p->outTail = NULL;
    }
    p->retryWriteAt = 0;
}

int c4fRtcWantsWrite(const C4fRtc *rtc)
{
    uint64_t now = c4fTimeMs();
    for (int i = 0; i < C4F_RTC_PEERS; i++) {
        const C4fRtcPeer *p = rtc->peers[i];
        if (p && !p->ending && !p->failed && p->out && now >= p->retryWriteAt) return 1;
    }
    return 0;
}

void c4fRtcWritable(C4fRtc *rtc)
{
    uint64_t now = c4fTimeMs();
    unsigned first = rtc->writeCursor++ % C4F_RTC_PEERS;
    for (unsigned i = 0; i < C4F_RTC_PEERS; i++) {
        C4fRtcPeer *p = rtc->peers[(first + i) % C4F_RTC_PEERS];
        if (p) c4fRtcDrainPeer(p, now);
    }
}

static int c4fRtcBioSend(void *ctx, const unsigned char *buf, size_t len)
{
    C4fRtcPeer *p = ctx;
    /* Accept a complete owned ciphertext datagram before telling DTLS that
     * its write succeeded. A later TLS write can never overwrite a blocked one. */
    if (!p->addrKnown || c4fRtcSendDatagram(p, buf, len, &p->addr)) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return (int)len;
}

static int c4fRtcBioRecv(void *ctx, unsigned char *buf, size_t len)
{
    C4fRtcPeer *p = ctx;
    if (!p->inLen) return MBEDTLS_ERR_SSL_WANT_READ;
    size_t n = p->inLen < len ? p->inLen : len;
    memcpy(buf, p->in, n);
    p->inLen = 0;
    return (int)n;
}

static int c4fRtcSctpSend(void *user, const uint8_t *packet, size_t len)
{
    C4fRtcPeer *p = user;
    if (p->ending || p->failed || !p->dtlsUp) return -1;
    int ret = mbedtls_ssl_write(&p->ssl, packet, len);
    if (ret != (int)len) {
        if (!p->failed) { p->failed = 1; p->failure = "DTLS send failed"; }
        return -1;
    }
    return 0;
}

static void c4fRtcSctpRandom(void *user, void *out, size_t len)
{
    C4fRtcPeer *p = user;
    if (mbedtls_ctr_drbg_random(&p->rtc->crypto->drbg, out, len)) arc4random_buf(out, len);
}

static void c4fRtcOpened(C4fRtcPeer *p)
{
    p->opened = 1;
    c4fLog("rtc: peer %u open in %llu ms\n", p->id, (unsigned long long)(c4fTimeMs() - p->createdAt));
    p->rtc->handler(&p->conn, C4F_NET_OPEN, NULL, 0, p->rtc->context);
}

static void c4fRtcSctpDeliver(void *user, uint16_t stream, uint32_t ppid, const uint8_t *data, size_t len)
{
    static char text[C4F_RTC_TEXT + 1];
    C4fRtcPeer *p = user;
    if (p->ending) return;
    if (!p->opened) c4fRtcOpened(p);
    if (ppid != C4F_SCTP_PPID_STRING || len > C4F_RTC_TEXT) return;
    memcpy(text, data, len);
    text[len] = 0;
    p->rtc->handler(&p->conn, stream == 1 ? C4F_NET_INPUT_EVENT : C4F_NET_MESSAGE_EVENT, text, len, p->rtc->context);
}

static int c4fRtcSend(C4fConn *c, const char *text, uint16_t stream)
{
    C4fRtcPeer *p = (C4fRtcPeer *)c;
    if (p->ending || p->failed || !p->opened) return -1;
    if (!c4fSctpSend(&p->sctp, stream, C4F_SCTP_PPID_STRING, (const uint8_t *)text, strlen(text), c4fTimeMs())) return 0;
    /* A reply that can't be queued means the page lost too much: let it reconnect. */
    if (stream == 0) { p->failed = 1; p->failure = "too much waiting to be sent"; }
    return -1;
}

static int c4fRtcText(C4fConn *c, const char *text) { return c4fRtcSend(c, text, 0); }
static int c4fRtcInput(C4fConn *c, const char *text) { return c4fRtcSend(c, text, 1); }

/* Ends a peer: the service hears of it now, the memory goes on the next tick. */
static void c4fRtcEnd(C4fRtcPeer *p, const char *why)
{
    if (p->ending) return;
    p->ending = 1;
    c4fLog("rtc: peer %u closed (%s) after %llu s; datagrams %llu in, %llu out; sctp resent %llu, gave up %llu, "
           "duplicates %llu, bad checksums %llu\n", p->id, why, (unsigned long long)((c4fTimeMs() - p->createdAt) / 1000),
           (unsigned long long)p->datagramsIn, (unsigned long long)p->datagramsOut,
           (unsigned long long)p->sctp.resent, (unsigned long long)p->sctp.abandoned,
           (unsigned long long)p->sctp.duplicates, (unsigned long long)p->sctp.badChecksums);
    if (p->opened) p->rtc->handler(&p->conn, C4F_NET_CLOSE, NULL, 0, p->rtc->context);
}

static void c4fRtcFreePeer(C4fRtc *rtc, int slot)
{
    C4fRtcPeer *p = rtc->peers[slot];
    rtc->peers[slot] = NULL;
    if (p->dtlsUp && rtc->fd >= 0) (void)mbedtls_ssl_close_notify(&p->ssl);
    mbedtls_ssl_free(&p->ssl);
    c4fSctpFree(&p->sctp);
    while (p->out) { C4fRtcDatagram *d = p->out; p->out = d->next; free(d); }
    free(p);
}

/* After SCTP had a packet or a tick: open, or ended by the browser. */
static void c4fRtcAfterSctp(C4fRtcPeer *p)
{
    if (p->ending) return;
    if (!p->opened && p->sctp.state == C4F_SCTP_ESTABLISHED) c4fRtcOpened(p);
    if (p->sctp.state == C4F_SCTP_ENDED) c4fRtcEnd(p, "the page closed it");
}

/* ------------------------------------------------------------ datagrams */

static void c4fRtcStun(C4fRtc *rtc, const uint8_t *msg, size_t len, const struct sockaddr_in *from, uint64_t now)
{
    size_t size;
    const uint8_t *user = c4fStunUsername(msg, len, &size);
    if (!user) return;
    const uint8_t *colon = memchr(user, ':', size);
    if (!colon) return;
    size_t ours = (size_t)(colon - user), theirs = size - ours - 1;
    for (int i = 0; i < C4F_RTC_PEERS; i++) {
        C4fRtcPeer *p = rtc->peers[i];
        if (!p || p->ending || strlen(p->localUfrag) != ours || memcmp(p->localUfrag, user, ours) ||
            strlen(p->remoteUfrag) != theirs || memcmp(p->remoteUfrag, colon + 1, theirs)) continue;
        uint8_t answer[C4F_STUN_ANSWER_SIZE];
        int nominate;
        size_t n = c4fStunAnswer(msg, len, p->localPwd, from->sin_addr.s_addr, from->sin_port, answer, &nominate);
        if (!n) return;
        p->datagramsIn++;
        (void)c4fRtcSendDatagram(p, answer, n, from);
        p->heardAt = now;
        /* Only a check signed with our password may move where the peer is. */
        if (!p->addrKnown || nominate) {
            p->addr = *from;
            p->addrKnown = 1;
        }
        return;
    }
}

static void c4fRtcDtls(C4fRtcPeer *p, const uint8_t *data, size_t len, uint64_t now)
{
    static unsigned char plain[2048];
    p->heardAt = now;
    p->datagramsIn++;
    p->in = data;
    p->inLen = len;
    if (!p->dtlsUp) {
        int ret = mbedtls_ssl_handshake(&p->ssl);
        if (ret == 0) {
            /* Is this the browser that sent the offer? */
            const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&p->ssl);
            unsigned char hash[32];
            if (!peer || mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0) ||
                memcmp(hash, p->remoteFingerprint, sizeof(hash))) {
                c4fRtcEnd(p, "its certificate doesn't match its offer");
                p->inLen = 0;
                return;
            }
            p->dtlsUp = 1;
            c4fLog("rtc: peer %u secured in %llu ms\n", p->id, (unsigned long long)(now - p->createdAt));
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char why[48];
            snprintf(why, sizeof(why), "DTLS handshake failed: -0x%04x", (unsigned)-ret);
            c4fRtcEnd(p, why);
            p->inLen = 0;
            return;
        }
    }
    while (p->dtlsUp && !p->ending) {
        int ret = mbedtls_ssl_read(&p->ssl, plain, sizeof(plain));
        if (ret > 0) {
            c4fSctpReceive(&p->sctp, plain, (size_t)ret, now);
            c4fRtcAfterSctp(p);
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) break;
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { c4fRtcEnd(p, "the page closed it"); break; }
        char why[40];
        snprintf(why, sizeof(why), "DTLS read failed: -0x%04x", (unsigned)-ret);
        c4fRtcEnd(p, why);
        break;
    }
    p->inLen = 0;
}

void c4fRtcReadable(C4fRtc *rtc)
{
    static uint8_t buf[2048];
    for (int i = 0; i < 64 && rtc->fd >= 0; i++) {
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(rtc->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromLen);
        if (n <= 0) break;
        uint64_t now = c4fTimeMs();
        if (c4fIsStun(buf, (size_t)n)) { c4fRtcStun(rtc, buf, (size_t)n, &from, now); continue; }
        if (!c4fIsDtls(buf, (size_t)n)) continue;
        for (int k = 0; k < C4F_RTC_PEERS; k++) {
            C4fRtcPeer *p = rtc->peers[k];
            if (p && !p->ending && p->addrKnown && p->addr.sin_addr.s_addr == from.sin_addr.s_addr &&
                p->addr.sin_port == from.sin_port) {
                c4fRtcDtls(p, buf, (size_t)n, now);
                break;
            }
        }
    }
}

void c4fRtcTick(C4fRtc *rtc, uint64_t now)
{
    for (int i = 0; i < C4F_RTC_PEERS; i++) {
        C4fRtcPeer *p = rtc->peers[i];
        if (!p) continue;
        if (!p->ending && p->failed) c4fRtcEnd(p, p->failure ? p->failure : "it failed");
        if (!p->ending && !p->dtlsUp && c4fRtcPeerTimer(p) == 2) {
            /* A handshake flight went unanswered: Mbed TLS sends it again. */
            int ret = mbedtls_ssl_handshake(&p->ssl);
            if (ret && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) c4fRtcEnd(p, "DTLS handshake timed out");
        }
        if (!p->ending && p->dtlsUp) { c4fSctpTick(&p->sctp, now); c4fRtcAfterSctp(p); }
        if (!p->ending && now - p->heardAt > C4F_RTC_SILENT_MS) c4fRtcEnd(p, "nothing heard for 10 s");
        if (!p->ending && !p->opened && now - p->createdAt > C4F_RTC_SETUP_MS) c4fRtcEnd(p, "it never connected");
        if (p->ending) c4fRtcFreePeer(rtc, i);
    }
}

int c4fRtcWait(const C4fRtc *rtc, uint64_t now, int limit)
{
    uint64_t soonest = now + (uint64_t)limit;
    for (int i = 0; i < C4F_RTC_PEERS; i++) {
        const C4fRtcPeer *p = rtc->peers[i];
        if (!p) continue;
        if (p->ending || p->failed) return 0;
        uint64_t at = p->heardAt + C4F_RTC_SILENT_MS + 1;
        if (at < soonest) soonest = at;
        if (p->out && p->retryWriteAt > now && p->retryWriteAt < soonest) soonest = p->retryWriteAt;
        if (!p->dtlsUp && p->timerFin && p->timerAt + p->timerFin < soonest) soonest = p->timerAt + p->timerFin;
        uint64_t sctp = c4fSctpDeadline(&p->sctp);
        if (sctp && sctp < soonest) soonest = sctp;
    }
    return soonest <= now ? 0 : (int)(soonest - now);
}

/* ------------------------------------------------------------ offer and answer */

/* Copies an "a=name:value" line's value. */
static int c4fSdpValue(const char *line, size_t len, const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    if (len < n || memcmp(line, name, n)) return 0;
    size_t v = len - n;
    if (v >= size) return -1;
    memcpy(out, line + n, v);
    out[v] = 0;
    return 1;
}

static int c4fContains(const char *text, size_t len, const char *word)
{
    size_t n = strlen(word);
    for (size_t i = 0; i + n <= len; i++) if (!memcmp(text + i, word, n)) return 1;
    return 0;
}

static int c4fHex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void c4fRtcToken(C4fRtc *rtc, char *out, size_t chars)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char raw[32];
    if (mbedtls_ctr_drbg_random(&rtc->crypto->drbg, raw, chars)) arc4random_buf(raw, chars);
    for (size_t i = 0; i < chars; i++) out[i] = alphabet[raw[i] % 62];
    out[chars] = 0;
}

int c4fRtcOffer(C4fRtc *rtc, const char *offer, size_t len, uint32_t local, char *answer, size_t size)
{
    char ufrag[257] = "", mid[33] = "0", setup[16] = "", fingerprint[128] = "", value[128];
    int application = 0, other = 0, bundle = 0;
    if (!rtc->crypto || rtc->fd < 0) return 503;
    for (size_t at = 0; at < len; ) {
        size_t end = at;
        while (end < len && offer[end] != '\n') end++;
        size_t n = end - at;
        const char *line = offer + at;
        if (n && line[n - 1] == '\r') n--;
        if (n > 2 && line[0] == 'm' && line[1] == '=') {
            if (n >= 14 && !memcmp(line, "m=application ", 14) && c4fContains(line, n, "webrtc-datachannel")) application++;
            else other++;
        }
        else if (c4fSdpValue(line, n, "a=ice-ufrag:", ufrag, sizeof(ufrag)) < 0) return 400;
        else if (c4fSdpValue(line, n, "a=mid:", mid, sizeof(mid)) < 0) return 400;
        else if (c4fSdpValue(line, n, "a=setup:", setup, sizeof(setup)) < 0) return 400;
        else if (c4fSdpValue(line, n, "a=fingerprint:", value, sizeof(value)) > 0) {
            if (!strncasecmp(value, "sha-256 ", 8) && !fingerprint[0]) snprintf(fingerprint, sizeof(fingerprint), "%s", value + 8);
        }
        else if (n >= 15 && !memcmp(line, "a=group:BUNDLE ", 15)) bundle = 1;
        at = end + 1;
    }
    /* One data-channel section, credentials, and a SHA-256 fingerprint. */
    if (application != 1 || other || strlen(ufrag) < 4 || !strcmp(setup, "passive") || strlen(fingerprint) != 95) return 400;
    for (size_t i = 0; mid[i]; i++) if (mid[i] <= ' ' || mid[i] > '~') return 400;
    for (size_t i = 0; ufrag[i]; i++) if (ufrag[i] <= ' ' || ufrag[i] > '~' || ufrag[i] == ':') return 400;
    uint8_t remote[32];
    for (int i = 0; i < 32; i++) {
        int hi = c4fHex(fingerprint[3 * i]), lo = c4fHex(fingerprint[3 * i + 1]);
        if (hi < 0 || lo < 0 || (i < 31 && fingerprint[3 * i + 2] != ':')) return 400;
        remote[i] = (uint8_t)(hi << 4 | lo);
    }
    int slot = -1;
    for (int i = 0; i < C4F_RTC_PEERS && slot < 0; i++) if (!rtc->peers[i]) slot = i;
    if (slot < 0) return 503;

    C4fRtcPeer *p = calloc(1, sizeof(*p));
    if (!p) return 503;
    p->conn.text = c4fRtcText;
    p->conn.input = c4fRtcInput;
    p->rtc = rtc;
    p->id = ++rtc->nextId;
    c4fRtcToken(rtc, p->localUfrag, 8);
    c4fRtcToken(rtc, p->localPwd, 24);
    snprintf(p->remoteUfrag, sizeof(p->remoteUfrag), "%s", ufrag);
    memcpy(p->remoteFingerprint, remote, sizeof(remote));
    p->createdAt = p->heardAt = c4fTimeMs();
    mbedtls_ssl_init(&p->ssl);
    if (mbedtls_ssl_setup(&p->ssl, &rtc->crypto->conf)) { mbedtls_ssl_free(&p->ssl); free(p); return 503; }
    mbedtls_ssl_set_bio(&p->ssl, p, c4fRtcBioSend, c4fRtcBioRecv, NULL);
    mbedtls_ssl_set_timer_cb(&p->ssl, p, c4fRtcPeerSetTimer, c4fRtcPeerTimer);
    mbedtls_ssl_set_mtu(&p->ssl, C4F_RTC_MTU);
    C4fSctpOps ops = { p, c4fRtcSctpSend, c4fRtcSctpDeliver, c4fRtcSctpRandom };
    c4fSctpInit(&p->sctp, &ops);

    char ip[INET_ADDRSTRLEN];
    struct in_addr address = { .s_addr = local };
    inet_ntop(AF_INET, &address, ip, sizeof(ip));
    unsigned long long session;
    c4fRtcSctpRandom(p, &session, sizeof(session));
    int n = snprintf(answer, size,
        "v=0\r\n"
        "o=- %llu 2 IN IP4 0.0.0.0\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "%s%s%s"
        "a=ice-lite\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=mid:%s\r\n"
        "a=ice-ufrag:%s\r\n"
        "a=ice-pwd:%s\r\n"
        "a=fingerprint:sha-256 %s\r\n"
        "a=setup:passive\r\n"
        "a=sctp-port:5000\r\n"
        "a=max-message-size:%d\r\n"
        "a=candidate:1 1 udp 2130706431 %s %d typ host\r\n"
        "a=end-of-candidates\r\n",
        session & 0x7FFFFFFFFFFFFFFFull, bundle ? "a=group:BUNDLE " : "", bundle ? mid : "", bundle ? "\r\n" : "",
        mid, p->localUfrag, p->localPwd, rtc->fingerprint, C4F_SCTP_MAX_MESSAGE, ip, rtc->port);
    if (n < 0 || (size_t)n >= size) { mbedtls_ssl_free(&p->ssl); c4fSctpFree(&p->sctp); free(p); return 503; }
    rtc->peers[slot] = p;
    c4fLog("rtc: peer %u offered, answering with %s:%d\n", p->id, ip, rtc->port);
    return 0;
}

/* ------------------------------------------------------------ socket */

int c4fRtcOpen(C4fRtc *rtc, int port, C4fNetHandler handler, void *context)
{
    rtc->handler = handler;
    rtc->context = context;
    rtc->port = port;
    rtc->fd = -1;
    if (!rtc->crypto && c4fRtcMakeCrypto(rtc)) return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0), one = 1;
    if (fd < 0) return -1;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_ANY) };
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (fd >= (int)FD_SETSIZE || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) || fcntl(fd, F_SETFL, O_NONBLOCK)) {
        c4fLog("rtc: UDP port %d unavailable, errno %d\n", port, errno);
        close(fd);
        return -1;
    }
    rtc->fd = fd;
    return 0;
}

void c4fRtcClose(C4fRtc *rtc)
{
    for (int i = 0; i < C4F_RTC_PEERS; i++) {
        if (!rtc->peers[i]) continue;
        c4fRtcEnd(rtc->peers[i], "the service reset its connections");
        c4fRtcFreePeer(rtc, i);
    }
    if (rtc->fd >= 0) close(rtc->fd);
    rtc->fd = -1;
}

void c4fRtcDestroy(C4fRtc *rtc)
{
    c4fRtcClose(rtc);
    if (rtc->crypto) { c4fRtcFreeCrypto(rtc->crypto); rtc->crypto = NULL; }
    memset(rtc->fingerprint, 0, sizeof(rtc->fingerprint));
}
