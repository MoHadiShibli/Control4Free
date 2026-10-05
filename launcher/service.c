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

/* OpenOrbis's time.h gives CLOCK_MONOTONIC the Linux value, 1. The PS4's kernel
 * is FreeBSD's, and there 1 is CLOCK_VIRTUAL: this process's own CPU time, which
 * an app that mostly sleeps hardly uses. The status check due every 2 seconds
 * then never came round, and the screen kept its first reading until the app was
 * reopened. 4 is FreeBSD's CLOCK_MONOTONIC, the value the payload's SDK uses. */
#ifdef __FreeBSD__
#define C4F_CLOCK_MONOTONIC 4
#else
#define C4F_CLOCK_MONOTONIC CLOCK_MONOTONIC
#endif

static char c4fHost[16];
static char c4fProblem[128];
static int c4fReached;

uint64_t c4fLauncherTimeMs(void)
{
    struct timespec t;
    clock_gettime(C4F_CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void c4fLauncherSetHost(const char *ipv4)
{
    struct in_addr parsed;
    if (ipv4 && inet_pton(AF_INET, ipv4, &parsed) == 1 && strcmp(ipv4, "127.0.0.1"))
        snprintf(c4fHost, sizeof(c4fHost), "%s", ipv4);
    else
        c4fHost[0] = 0;
}

const char *c4fLauncherProblem(void) { return c4fProblem; }

static void c4fNote(const char *address, const char *what)
{
    size_t used = strlen(c4fProblem);
    snprintf(c4fProblem + used, sizeof(c4fProblem) - used, "%s%s %s", used ? "; " : "", address, what);
}

/* Loopback first, then the console's own address. */
static int c4fTargets(const char *targets[2])
{
    targets[0] = "127.0.0.1";
    if (!c4fHost[0]) return 1;
    targets[1] = c4fHost;
    return 2;
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

static int c4fConnect(const char *address, int port)
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
    if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) { errno = EINVAL; goto fail; }
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
#if defined(MSG_NOSIGNAL) && defined(__linux__) /* OpenOrbis gives it a Linux value */
        flags = MSG_NOSIGNAL;
#endif
        ssize_t n = send(fd, p, size, flags);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}

/* One request on a connected socket (closed here); 1 with the body on 200 OK. */
static int c4fExchange(int fd, const char *address, const char *method, const char *path, char *body, size_t capacity)
{
    char request[256], response[2048], what[48];
    int n = snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: %s:%d\r\nX-Control4Free-Launcher: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", method, path, address, C4F_SERVICE_PORT);
    uint64_t deadline = c4fLauncherTimeMs() + 1500;
    if (c4fSendAll(fd, request, (size_t)n, deadline)) {
        snprintf(what, sizeof(what), "send errno %d", errno);
        c4fNote(address, what); close(fd); return -1;
    }
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
    int code = 0;
    if (!used) { c4fNote(address, "no reply"); return -1; }
    if (sscanf(response, "HTTP/1.1 %d", &code) != 1) { c4fNote(address, "not HTTP"); return -1; }
    if (code != 200) { snprintf(what, sizeof(what), "HTTP %d", code); c4fNote(address, what); return -1; }
    char *start = strstr(response, "\r\n\r\n");
    if (!complete || !start) { c4fNote(address, "reply cut off"); return -1; }
    start += 4;
    size_t length = used - (size_t)(start - response);
    if (!length || length >= capacity || memchr(start, 0, length)) { c4fNote(address, "bad reply"); return -1; }
    memcpy(body, start, length); body[length] = 0;
    return 1;
}

/* 1: 200 OK with a body. 0: no address accepted a connection and at least one
 * refused it, so nothing listens (the service binds every address). -1: any
 * other outcome, explained in c4fProblem. */
static int c4fHttp(const char *method, const char *path, char *body, size_t capacity)
{
    const char *targets[2];
    int count = c4fTargets(targets), refused = 0, connected = 0;
    c4fProblem[0] = 0;
    for (int i = 0; i < count; i++) {
        int fd = c4fConnect(targets[i], C4F_SERVICE_PORT);
        if (fd < 0) {
            char what[40];
            if (errno == ECONNREFUSED) { refused++; snprintf(what, sizeof(what), "refused"); }
            else snprintf(what, sizeof(what), "connect errno %d", errno);
            c4fNote(targets[i], what);
            continue;
        }
        connected++;
        if (c4fExchange(fd, targets[i], method, path, body, capacity) == 1) { c4fReached = 1; return 1; }
    }
    c4fReached = connected > 0;
    return !connected && refused ? 0 : -1;
}

int c4fLauncherReached(void) { return c4fReached; }

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
    snprintf(c4fProblem, sizeof(c4fProblem), "the status reply is not from Control4Free");
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
    if (!(app && api && controllers && stopping)) return -1;
    c4fProblem[0] = 0;
    return 1;
}

int c4fLauncherStart(const unsigned char *payload, size_t payloadSize, char *message, size_t size)
{
    C4fServiceStatus status;
    int probe = c4fLauncherProbe(&status);
    if (probe == 1) { snprintf(message, size, "Already running. Open the address on your phone or PC."); return 0; }
    if (probe != 0 && !c4fReached) { snprintf(message, size, "The app cannot check whether Control4Free is running (%s).", c4fProblem); return -1; }
    if (probe != 0) { snprintf(message, size, "Control4Free is not responding (%s). Wait a moment after waking. If it stays stuck, restart the PS4.", c4fProblem); return -1; }
    if (!payload) { snprintf(message, size, "The payload is missing. Reinstall the Control4Free package."); return -1; }
    if (payloadSize < 64 || memcmp(payload, "\177ELF", 4) || payload[4] != 2 || payload[5] != 1) {
        snprintf(message, size, "The bundled payload is damaged. Reinstall the Control4Free package."); return -1;
    }
    const char *targets[2];
    int count = c4fTargets(targets), fd = -1;
    for (int i = 0; i < count && fd < 0; i++) fd = c4fConnect(targets[i], C4F_PAYLOADER_PORT);
    if (fd < 0) { snprintf(message, size, "PayLoader did not answer (errno %d). Turn it on in GoldHEN, then press Cross again.", errno); return -1; }
    int failed = c4fSendAll(fd, payload, payloadSize, c4fLauncherTimeMs() + 10000);
    shutdown(fd, SHUT_WR); close(fd);
    if (failed) { snprintf(message, size, "The transfer to PayLoader broke off. Restart the PS4 before you try again."); return -2; }
    uint64_t deadline;
    deadline = c4fLauncherTimeMs() + 20000;
    while (c4fLauncherTimeMs() < deadline) {
        if (c4fLauncherProbe(&status) == 1 && !status.stopping) {
            snprintf(message, size, "Ready. Open the address on your phone or PC."); return 0;
        }
        usleep(250000);
    }
    snprintf(message, size, "Sent, but Control4Free never answered (%s). Restart the PS4 before you try again.", c4fProblem);
    return -2; /* Caller locks further sends until restart, avoiding duplicates. */
}

int c4fLauncherStop(char *message, size_t size)
{
    C4fServiceStatus status;
    int probe = c4fLauncherProbe(&status);
    if (probe == 0) { snprintf(message, size, "Stopped. Press Cross to start it again."); return 0; }
    if (probe != 1 && !c4fReached) { snprintf(message, size, "The app cannot reach Control4Free (%s). Stop it on its controller page.", c4fProblem); return -1; }
    if (probe != 1) { snprintf(message, size, "This copy does not answer the app (%s). Stop it on its controller page.", c4fProblem); return -1; }
    char reply[256];
    int ret = c4fHttp("POST", "/api/stop", reply, sizeof(reply));
    if (ret != 1) { snprintf(message, size, "Stop was not confirmed (%s). Check the controller page.", c4fProblem); return -1; }
    uint64_t deadline = c4fLauncherTimeMs() + 5000;
    while (c4fLauncherTimeMs() < deadline) {
        if (c4fLauncherProbe(&status) == 0) { snprintf(message, size, "Stopped. Press Cross to start it again."); return 0; }
        usleep(100000);
    }
    snprintf(message, size, "Still stopping. Wait a moment, then try again."); return -1;
}
