#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps4/klog.h>

#include "c4f_log.h"
#include "c4f_sce.h"
#include "c4f_version.h"

static int g_logFd = -1;
static int g_klogEnabled = 1;
static uint64_t g_startedMs;

static uint64_t c4fLogNowMs(void)
{
    struct timespec now = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void c4fLogSetKlog(int enabled) { g_klogEnabled = enabled; }
int  c4fLogKlogEnabled(void) { return g_klogEnabled; }

void c4fLogOpen(void)
{
    (void)mkdir("/data", 0777);
    (void)mkdir(C4F_LOG_DIR, 0777);
    /* Keep the previous run for post-restart diagnosis. */
    if (rename(C4F_LOG_PATH, C4F_LOG_PATH ".previous") != 0 && errno != ENOENT)
        g_logFd = open(C4F_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    else
        g_logFd = open(C4F_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    g_startedMs = c4fLogNowMs();
    c4fLog("---- Control4Free %s; timestamps are elapsed time ----\n", C4F_VERSION);
}

void c4fLogClose(void)
{
    if (g_logFd < 0) return;
    c4fLog("---- end ----\n");
    (void)fsync(g_logFd);
    (void)close(g_logFd);
    g_logFd = -1;
}

void c4fLog(const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(msg)) n = (int)sizeof(msg) - 1;

    uint64_t now = c4fLogNowMs();
    uint64_t elapsed = now >= g_startedMs ? now - g_startedMs : 0;
    /* Prefix every line, including multiline diagnostics and truncated output. */
    for (const char *p = msg; p < msg + n; ) {
        const char *end = memchr(p, '\n', (size_t)(msg + n - p));
        size_t length = end ? (size_t)(end - p) : (size_t)(msg + n - p);
        char line[1152];
        int used = snprintf(line, sizeof(line), "[c4f] [+%02llu:%02llu:%02llu.%03llu] %.*s\n",
                            (unsigned long long)(elapsed / 3600000),
                            (unsigned long long)(elapsed / 60000 % 60),
                            (unsigned long long)(elapsed / 1000 % 60),
                            (unsigned long long)(elapsed % 1000), (int)length, p);
        if (g_klogEnabled) klog_printf("%s", line);
        if (g_logFd >= 0) {
            size_t offset = 0;
            while (offset < (size_t)used) {
                ssize_t written = write(g_logFd, line + offset, (size_t)used - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) break;
                offset += (size_t)written;
            }
        }
        p += length + (end != NULL);
    }
}

/* The notification request is mostly opaque; only the message matters. */
typedef struct {
    char unused[45];
    char message[3075];
} C4fNotifyRequest;

void c4fNotify(const char *fmt, ...)
{
    C4fNotifyRequest req;
    va_list ap;

    (void)memset(&req, 0, sizeof(req));
    va_start(ap, fmt);
    (void)vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);
#ifdef C4F_DIAG
    int ret = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    c4fLog("notification = 0x%08x: %s\n", (uint32_t)ret, req.message);
#else
    (void)sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
#endif
}
