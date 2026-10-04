/* Bounded HTTP/WebSocket transport for the embedded controller page. */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "c4f_net.h"

/* Which interface would carry traffic out, without sending anything: a connected
 * UDP socket picks the route, and getsockname then names the local end. */
void c4fNetLocalAddress(char *out, size_t size)
{
    struct sockaddr_in remote = {0}, local = {0};
    socklen_t length = sizeof(local);
    int fd;

    if (size) out[0] = 0;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = inet_addr("1.1.1.1");
    if (!connect(fd, (struct sockaddr *)&remote, sizeof(remote)) &&
        !getsockname(fd, (struct sockaddr *)&local, &length))
        (void)inet_ntop(AF_INET, &local.sin_addr, out, size);
    close(fd);
}

uint64_t c4fTimeMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static uint32_t c4fRol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }

/* RFC 6455's SHA-1 + base64 handshake. Keys are fixed at 24 ASCII bytes; this
 * implementation hashes only the key and protocol GUID, never arbitrary data. */
void c4fWebSocketAccept(const char *key, char out[29])
{
    unsigned char msg[128] = {0}, digest[20];
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned block, i, j = 0;
    memcpy(msg, key, 24);
    memcpy(msg + 24, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    msg[60] = 0x80;
    msg[126] = 1; msg[127] = 0xe0; /* 60 * 8 bits */
    for (block = 0; block < 2; block++) {
        uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (i = 0; i < 16; i++) {
            const unsigned char *p = msg + block * 64 + i * 4;
            w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }
        for (i = 16; i < 80; i++) w[i] = c4fRol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        for (i = 0; i < 80; i++) {
            uint32_t f, k, t;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            t = c4fRol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = c4fRol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (i = 0; i < 20; i++) digest[i] = (unsigned char)(h[i/4] >> (24 - 8*(i%4)));
    for (i = 0; i < 20; i += 3) {
        uint32_t v = (uint32_t)digest[i] << 16;
        if (i+1 < 20) v |= (uint32_t)digest[i+1] << 8;
        if (i+2 < 20) v |= digest[i+2];
        out[j++] = b64[v >> 18]; out[j++] = b64[(v >> 12) & 63];
        out[j++] = i+1 < 20 ? b64[(v >> 6) & 63] : '=';
        out[j++] = i+2 < 20 ? b64[v & 63] : '=';
    }
    out[j] = 0;
}

static int c4fQueue(C4fNetClient *c, const void *data, size_t n)
{
    if (c->closing || c->body) return -1;
    if (c->txSent) {
        memmove(c->tx, c->tx + c->txSent, c->txUsed - c->txSent);
        c->txUsed -= c->txSent; c->txSent = 0;
    }
    if (n > sizeof(c->tx) - c->txUsed) { c->closing = 1; return -1; }
    if (n) memcpy(c->tx + c->txUsed, data, n);
    c->txUsed += n;
    return 0;
}

static int c4fFrame(C4fNetClient *c, unsigned op, const void *data, size_t n)
{
    unsigned char head[4] = { (unsigned char)(0x80 | op), (unsigned char)n, 0, 0 };
    size_t h = 2;
    if (n >= 126) { head[1] = 126; head[2] = n >> 8; head[3] = n; h = 4; }
    if (c4fQueue(c, head, h) != 0) return -1;
    return c4fQueue(c, data, n);
}

int c4fNetText(C4fNetClient *c, const char *text) { return c4fFrame(c, 1, text, strlen(text)); }

void c4fNetHttpJson(C4fNetClient *c, const char *json)
{
    char header[256];
    int n = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", strlen(json));
    if (c4fQueue(c, header, (size_t)n) || c4fQueue(c, json, strlen(json))) return;
    c->closing = 2;
}

static void c4fDrop(C4fNet *net, C4fNetClient *c)
{
    if (c->websocket) net->handler(c, C4F_NET_CLOSE, NULL, 0, net->context);
    close(c->fd);
    memset(c, 0, sizeof(*c)); c->fd = -1;
}

/* Header values are copied and duplicates are rejected. */
static int c4fHeader(const char *request, const char *key, char *value, size_t size)
{
    const char *p = strstr(request, "\r\n");
    size_t k = strlen(key);
    int found = 0;
    value[0] = 0;
    while (p && p[2] && p[2] != '\r') {
        const char *end, *start;
        p += 2; end = strstr(p, "\r\n");
        if (!end) return -1;
        if ((size_t)(end - p) > k && !strncasecmp(p, key, k) && p[k] == ':') {
            if (found++) return -1;
            start = p + k + 1;
            while (start < end && (*start == ' ' || *start == '\t')) start++;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
            if ((size_t)(end-start) >= size) return -1;
            memcpy(value, start, end-start); value[end-start] = 0;
        }
        p = strstr(p, "\r\n");
    }
    return found;
}

static void c4fHttpError(C4fNetClient *c, int code, const char *message)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "HTTP/1.1 %d Error\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", code, strlen(message), message);
    c4fQueue(c, buf, (size_t)n); c->closing = 2;
}

static void c4fHttp(C4fNet *net, C4fNetClient *c)
{
    char request[8193], host[128], origin[160], key[40], upgrade[32], version[12], connection[128];
    char *end;
    size_t used;
    memcpy(request, c->rx, c->rxUsed); request[c->rxUsed] = 0;
    end = strstr(request, "\r\n\r\n");
    if (!end) { if (c->rxUsed == sizeof(c->rx)) c4fHttpError(c, 431, "Headers too large"); return; }
    used = (size_t)(end-request) + 4;
    request[used] = 0;
    int statusRequest = !strncmp(request, "GET /api/status HTTP/1.1\r\n", 26);
    int stopRequest = !strncmp(request, "POST /api/stop HTTP/1.1\r\n", 25);
    /* The two files a phone needs to keep this page on its home screen. */
    const unsigned char *asset = NULL;
    size_t assetSize = 0;
    const char *assetType = NULL;
    if (!strncmp(request, "GET /manifest.webmanifest HTTP/1.1\r\n", 36)) {
        asset = c4fManifest; assetSize = c4fManifestSize; assetType = "application/manifest+json";
    } else if (!strncmp(request, "GET /icon-192.png HTTP/1.1\r\n", 28)) {
        asset = c4fIcon; assetSize = c4fIconSize; assetType = "image/png";
    }
    if (!statusRequest && !stopRequest && !asset && strncmp(request, "GET / HTTP/1.1\r\n", 16) &&
        strncmp(request, "GET /index.html HTTP/1.1\r\n", 26) &&
        strncmp(request, "GET /ws HTTP/1.1\r\n", 18) &&
        strncmp(request, "GET /?", 6)) {
        c4fHttpError(c, 404, "Not found"); return;
    }
    if (c4fHeader(request, "Host", host, sizeof(host)) != 1 ||
        c4fHeader(request, "Origin", origin, sizeof(origin)) < 0 ||
        c4fHeader(request, "Upgrade", upgrade, sizeof(upgrade)) < 0) {
        c4fHttpError(c, 400, "Invalid headers"); return;
    }
    /* Serve only literal IPv4 addresses or localhost: block DNS-rebinding hosts. */
    {
        char address[128], *colon; struct in_addr parsed;
        snprintf(address, sizeof(address), "%s", host);
        colon = strchr(address, ':'); if (colon) *colon = 0;
        if (strcmp(address, "localhost") && inet_pton(AF_INET, address, &parsed) != 1) {
            c4fHttpError(c, 403, "Open the PS4 IP address"); return;
        }
    }
    if (statusRequest || stopRequest) {
        char launcher[8], length[16], transfer[32];
        /* For the launcher app. A website cannot reach this: it would need the
         * custom header, which a browser only sends cross-origin after a CORS
         * preflight that is never answered, and every browser request carrying
         * an Origin is refused. The source address is not checked, because the
         * app's sandbox may not appear as 127.0.0.1; devices on the network can
         * already stop Control4Free from the page. */
        if (origin[0] || upgrade[0] ||
            c4fHeader(request, "X-Control4Free-Launcher", launcher, sizeof(launcher)) != 1 || strcmp(launcher, "1") ||
            c4fHeader(request, "Content-Length", length, sizeof(length)) < 0 || (length[0] && strcmp(length, "0")) ||
            c4fHeader(request, "Transfer-Encoding", transfer, sizeof(transfer)) != 0) {
            c4fHttpError(c, 403, "Launcher only"); return;
        }
        c->rxUsed = 0;
        net->handler(c, stopRequest ? C4F_NET_HTTP_STOP : C4F_NET_HTTP_STATUS, NULL, 0, net->context);
    } else if (upgrade[0]) {
        char accept[29], reply[256], expected[160];
        int n;
        snprintf(expected, sizeof(expected), "http://%s", host);
        if (origin[0] && strcmp(origin, "null") && strcmp(origin, expected)) {
            c4fHttpError(c, 403, "Origin not allowed"); return;
        }
        if (strcasecmp(upgrade, "websocket") ||
            c4fHeader(request, "Sec-WebSocket-Key", key, sizeof(key)) != 1 ||
            c4fHeader(request, "Sec-WebSocket-Version", version, sizeof(version)) != 1 ||
            c4fHeader(request, "Connection", connection, sizeof(connection)) != 1 ||
            strcmp(version, "13") || strlen(key) != 24 || strcmp(key+22, "==") ||
            strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") != 22) {
            c4fHttpError(c, 400, "Invalid WebSocket handshake"); return;
        }
        for (size_t i = 0; connection[i]; i++) connection[i] = (char)tolower((unsigned char)connection[i]);
        if (!strstr(connection, "upgrade")) { c4fHttpError(c, 400, "Upgrade required"); return; }
        c4fWebSocketAccept(key, accept);
        n = snprintf(reply, sizeof(reply), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
        c4fQueue(c, reply, (size_t)n); c->websocket = 1;
        memmove(c->rx, c->rx+used, c->rxUsed-used); c->rxUsed -= used;
        net->handler(c, C4F_NET_OPEN, NULL, 0, net->context);
    } else if (asset) {
        char reply[256];
        int n = snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: max-age=86400\r\nX-Content-Type-Options: nosniff\r\n\r\n", assetType, assetSize);
        c4fQueue(c, reply, (size_t)n);
        c->body = asset; c->bodySize = assetSize; c->rxUsed = 0;
    } else {
        char reply[768];
        int n = snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; media-src data:; connect-src ws:; manifest-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n\r\n", c4fPageSize);
        c4fQueue(c, reply, (size_t)n);
        c->body = c4fPage; c->bodySize = c4fPageSize; c->rxUsed = 0;
    }
}

static void c4fWs(C4fNet *net, C4fNetClient *c)
{
    while (c->rxUsed >= 2 && !c->closing) {
        unsigned char *p = c->rx;
        unsigned op = p[0] & 15, fin = p[0] >> 7;
        size_t n = p[1] & 127, head = 2;
        if ((p[0] & 0x70) || !(p[1] & 128)) { c->closing = 1; break; }
        if (n == 126) {
            if (c->rxUsed < 4) break;
            n = (size_t)p[2] << 8 | p[3]; head = 4;
            if (n < 126) { c->closing = 1; break; }
        } else if (n == 127) { c->closing = 1; break; } /* messages are capped at 4 KiB */
        if (n > C4F_NET_MESSAGE || (op >= 8 && (!fin || n > 125))) { c->closing = 1; break; }
        if (c->rxUsed < head + 4 + n) break;
        unsigned char *mask = p + head, *data = mask + 4;
        for (size_t i = 0; i < n; i++) data[i] ^= mask[i % 4];
        if (op == 8) {
            c4fFrame(c, 8, NULL, 0); c->closing = 2;
        } else if (op == 9) c4fFrame(c, 10, data, n);
        else if (op == 10) { /* pong */ }
        else if ((op == 1 && !c->fragmented) || (op == 0 && c->fragmented)) {
            if (n > C4F_NET_MESSAGE - c->messageUsed) { c->closing = 1; break; }
            memcpy(c->message+c->messageUsed, data, n); c->messageUsed += n;
            c->fragmented = !fin;
            if (fin) {
                c->message[c->messageUsed] = 0;
                net->handler(c, C4F_NET_MESSAGE_EVENT, c->message, c->messageUsed, net->context);
                c->messageUsed = 0;
            }
        } else { c->closing = 1; break; }
        size_t taken = head+4+n;
        memmove(c->rx, c->rx+taken, c->rxUsed-taken); c->rxUsed -= taken;
    }
}

int c4fNetOpen(C4fNet *net, int port, C4fNetHandler handler, void *context)
{
    struct sockaddr_in addr;
    int one = 1;
    memset(net, 0, sizeof(*net)); net->fd = -1;
    for (int i = 0; i < C4F_NET_CLIENTS; i++) net->clients[i].fd = -1;
    net->handler = handler; net->context = context;
    net->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (net->fd < 0) return -1;
    setsockopt(net->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
#ifdef __FreeBSD__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (net->fd >= (int)FD_SETSIZE || fcntl(net->fd, F_SETFL, O_NONBLOCK) ||
        bind(net->fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(net->fd, 8)) {
        close(net->fd); net->fd = -1; return -1;
    }
    return 0;
}

int c4fNetPoll(C4fNet *net, int timeoutMs)
{
    fd_set rd, wr;
    struct timeval timeout = { .tv_sec = 0, .tv_usec = timeoutMs * 1000 };
    int maxFd = net->fd, ready;
    uint64_t now = c4fTimeMs();
    if (now >= net->checkedMs) {
        int error = 0, listening = 0; socklen_t size = sizeof(error);
        if (getsockopt(net->fd, SOL_SOCKET, SO_ERROR, &error, &size)) return -1;
        if (error) { errno = error; return -1; }
        size = sizeof(listening);
        if (getsockopt(net->fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &size)) return -1;
        if (!listening) { errno = ENOTCONN; return -1; }
        net->checkedMs = now + 1000;
    }
    FD_ZERO(&rd); FD_ZERO(&wr); FD_SET(net->fd, &rd);
    for (int i = 0; i < C4F_NET_CLIENTS; i++) {
        C4fNetClient *c = &net->clients[i];
        if (c->fd < 0) continue;
        if (c->closing == 1) { c4fDrop(net, c); continue; }
        if (!c->closing && !c->body) FD_SET(c->fd, &rd);
        if (c->txSent < c->txUsed || c->body) FD_SET(c->fd, &wr);
        if (c->fd > maxFd) maxFd = c->fd;
    }
    uint64_t waitedFrom = c4fTimeMs();
    time_t wallFrom = time(NULL);
    ready = select(maxFd+1, &rd, &wr, NULL, &timeout);
    uint64_t waitedTo = c4fTimeMs();
    if (waitedTo < waitedFrom || waitedTo - waitedFrom > 5000 || time(NULL) - wallFrom > 5)
        return -2;
    if (ready < 0) return errno == EINTR ? 0 : -1;
    if (FD_ISSET(net->fd, &rd)) {
        struct sockaddr_in peer;
        socklen_t peerSize = sizeof(peer);
        int fd = accept(net->fd, (struct sockaddr *)&peer, &peerSize), index;
        if (fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return -1;
        for (index = 0; index < C4F_NET_CLIENTS && net->clients[index].fd >= 0; index++) {}
        if (fd >= 0) {
            if (index == C4F_NET_CLIENTS || fd >= (int)FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK)) close(fd);
            else {
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
                setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                net->clients[index].fd = fd; net->clients[index].openedMs = c4fTimeMs();
            }
        }
    }
    for (int i = 0; i < C4F_NET_CLIENTS; i++) {
        C4fNetClient *c = &net->clients[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, &rd)) {
            ssize_t n = recv(c->fd, c->rx+c->rxUsed, sizeof(c->rx)-c->rxUsed, 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) c->closing = 1;
            else if (n > 0) {
                c->rxUsed += (size_t)n;
                if (!c->websocket) c4fHttp(net, c);
                if (c->websocket) c4fWs(net, c);
            }
        }
        if (c->closing != 1 && FD_ISSET(c->fd, &wr)) {
            const unsigned char *data = c->tx+c->txSent;
            size_t left = c->txUsed-c->txSent;
            int body = left == 0, flags = 0;
            if (body) { data = c->body+c->bodySent; left = c->bodySize-c->bodySent; }
            if (left > 16384) left = 16384;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            ssize_t n = send(c->fd, data, left, flags);
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) c->closing = 1;
            else if (n > 0) {
                if (body) {
                    c->bodySent += (size_t)n;
                    if (c->bodySent == c->bodySize) { c->body = NULL; c->closing = 1; }
                } else { c->txSent += (size_t)n; if (c->txSent == c->txUsed) c->txSent = c->txUsed = 0; }
            }
        }
        if (!c->websocket && c4fTimeMs()-c->openedMs > 10000) c->closing = 1;
        if (c->closing == 1 || (c->closing == 2 && c->txSent == c->txUsed)) c4fDrop(net, c);
    }
    return 0;
}

void c4fNetClose(C4fNet *net)
{
    for (int i = 0; i < C4F_NET_CLIENTS; i++) if (net->clients[i].fd >= 0) c4fDrop(net, &net->clients[i]);
    if (net->fd >= 0) close(net->fd);
    net->fd = -1;
}
