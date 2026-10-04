/* Local-only launcher transport. No controller creation or input injection. */
#include "service.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

#ifndef C4F_SERVICE_PORT
#define C4F_SERVICE_PORT 4264
#endif
#ifndef C4F_PAYLOADER_PORT
#define C4F_PAYLOADER_PORT 9090
#endif

uint64_t c4fLauncherTimeMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int c4fWaitSocket(int fd, int writeReady, uint64_t deadline)
{
    for (;;) {
        uint64_t now = c4fLauncherTimeMs();
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        unsigned remaining = (unsigned)(deadline - now);
        struct timeval tv = { remaining / 1000, (remaining % 1000) * 1000 };
        int ret = select(fd + 1, writeReady ? NULL : &set, writeReady ? &set : NULL, NULL, &tv);
        if (ret > 0) return 0;
        if (ret < 0 && errno == EINTR) continue;
        if (ret == 0) errno = ETIMEDOUT;
        return -1;
    }
}

static int c4fConnect(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int savedError;
    if (fd >= (int)FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) goto fail;
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
    if (errno != EINPROGRESS) goto fail;
    if (c4fWaitSocket(fd, 1, c4fLauncherTimeMs() + 700)) goto fail;
    int error = 0; socklen_t size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) goto fail;
    if (error) { errno = error; goto fail; }
    return fd;
fail:
    savedError = errno; close(fd); errno = savedError; return -1;
}

static int c4fSendAll(int fd, const void *data, size_t size, uint64_t deadline)
{
    const char *p = data;
    while (size) {
        if (c4fWaitSocket(fd, 1, deadline)) return -1;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t n = send(fd, p, size, flags);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}

static int c4fHttp(const char *method, const char *path, char *body, size_t capacity)
{
    int fd = c4fConnect(C4F_SERVICE_PORT);
    if (fd < 0) return errno == ECONNREFUSED ? 0 : -1;
    char request[256], response[2048];
    int n = snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nX-Control4Free-Launcher: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", method, path, C4F_SERVICE_PORT);
    uint64_t deadline = c4fLauncherTimeMs() + 1500;
    if (c4fSendAll(fd, request, (size_t)n, deadline)) { close(fd); return -1; }
    size_t used = 0;
    int complete = 0;
    while (used < sizeof(response) - 1) {
        if (c4fWaitSocket(fd, 0, deadline)) break;
        ssize_t count = recv(fd, response + used, sizeof(response) - 1 - used, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count == 0) { complete = 1; break; }
        if (count < 0) break;
        used += (size_t)count;
    }
    close(fd); response[used] = 0;
    char *start = strstr(response, "\r\n\r\n");
    if (!complete || strncmp(response, "HTTP/1.1 200 OK\r\n", 17) || !start) return -1;
    start += 4;
    size_t length = used - (size_t)(start - response);
    if (!length || length >= capacity || memchr(start, 0, length)) return -1;
    memcpy(body, start, length); body[length] = 0;
    return 1;
}

static int c4fEquals(const char *json, const jsmntok_t *token, const char *value)
{
    return token->end - token->start == (int)strlen(value) && !memcmp(json + token->start, value, strlen(value));
}

int c4fLauncherProbe(C4fServiceStatus *status)
{
    char body[512]; jsmntok_t tokens[24]; jsmn_parser parser;
    memset(status, 0, sizeof(*status));
    int result = c4fHttp("GET", "/api/status", body, sizeof(body));
    if (result <= 0) return result;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, body, strlen(body), tokens, 24);
    if (count < 1 || tokens[0].type != JSMN_OBJECT || tokens[0].end != (int)strlen(body)) return -1;
    int app = 0, api = 0, controllers = 0, stopping = 0;
    for (int i = 1; i + 1 < count; i += 2) {
        jsmntok_t *key = &tokens[i], *value = &tokens[i+1];
        if (key->type != JSMN_STRING || (value->type != JSMN_STRING && value->type != JSMN_PRIMITIVE)) return -1;
        if (c4fEquals(body, key, "application")) app = value->type == JSMN_STRING && c4fEquals(body, value, "Control4Free");
        else if (c4fEquals(body, key, "api")) api = value->type == JSMN_PRIMITIVE && c4fEquals(body, value, "1");
        else if (c4fEquals(body, key, "controllers")) {
            if (value->type != JSMN_PRIMITIVE || value->end - value->start != 1 || body[value->start] < '0' || body[value->start] > '4') return -1;
            status->controllers = body[value->start] - '0'; controllers = 1;
        } else if (c4fEquals(body, key, "stopping")) {
            if (value->type != JSMN_PRIMITIVE || (!c4fEquals(body, value, "true") && !c4fEquals(body, value, "false"))) return -1;
            status->stopping = c4fEquals(body, value, "true"); stopping = 1;
        } else if (c4fEquals(body, key, "version")) {
            int size = value->end - value->start;
            if (value->type != JSMN_STRING || size >= (int)sizeof(status->version)) return -1;
            memcpy(status->version, body + value->start, (size_t)size);
        }
    }
    return app && api && controllers && stopping ? 1 : -1;
}

int c4fLauncherStart(const char *payload, char *message, size_t size)
{
    C4fServiceStatus status;
    int probe = c4fLauncherProbe(&status);
    if (probe == 1) { snprintf(message, size, "Already running. Scan the code to connect."); return 0; }
    if (probe != 0) { snprintf(message, size, "Existing service detected. Stop it from the phone first."); return -1; }
    FILE *file = fopen(payload, "rb");
    if (!file) { snprintf(message, size, "Payload missing. Reinstall the Control4Free package."); return -1; }
    unsigned char buffer[16384];
    size_t first = fread(buffer, 1, sizeof(buffer), file);
    if (first < 64 || memcmp(buffer, "\177ELF", 4) || buffer[4] != 2 || buffer[5] != 1) {
        fclose(file); snprintf(message, size, "Invalid bundled payload. Reinstall this package."); return -1;
    }
    int fd = c4fConnect(C4F_PAYLOADER_PORT);
    if (fd < 0) { fclose(file); snprintf(message, size, "Turn on GoldHEN's PayLoader, then press Cross again."); return -1; }
    uint64_t deadline = c4fLauncherTimeMs() + 10000;
    int failed = c4fSendAll(fd, buffer, first, deadline);
    while (!failed) {
        size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (!count) { failed = ferror(file); break; }
        failed = c4fSendAll(fd, buffer, count, deadline);
    }
    fclose(file); shutdown(fd, SHUT_WR); close(fd);
    if (failed) { snprintf(message, size, "Transfer interrupted. Restart the PS4 before retrying."); return -2; }
    deadline = c4fLauncherTimeMs() + 20000;
    while (c4fLauncherTimeMs() < deadline) {
        if (c4fLauncherProbe(&status) == 1 && !status.stopping) {
            snprintf(message, size, "Ready. Scan the code or open the address on your phone."); return 0;
        }
        usleep(250000);
    }
    snprintf(message, size, "Sent, but no reply. Restart the PS4 before retrying.");
    return -2; /* Caller locks further sends until restart, avoiding duplicates. */
}

int c4fLauncherStop(char *message, size_t size)
{
    C4fServiceStatus status;
    int probe = c4fLauncherProbe(&status);
    if (probe == 0) { snprintf(message, size, "Stopped. Press Cross to start it again."); return 0; }
    if (probe != 1) { snprintf(message, size, "Older service detected. Stop it from the phone."); return -1; }
    char reply[256];
    int ret = c4fHttp("POST", "/api/stop", reply, sizeof(reply));
    if (ret != 1) { snprintf(message, size, "Stop was not confirmed. Check the phone page."); return -1; }
    uint64_t deadline = c4fLauncherTimeMs() + 5000;
    while (c4fLauncherTimeMs() < deadline) {
        if (c4fLauncherProbe(&status) == 0) { snprintf(message, size, "Stopped. Press Cross to start it again."); return 0; }
        usleep(100000);
    }
    snprintf(message, size, "Still stopping. Wait before trying again."); return -1;
}
