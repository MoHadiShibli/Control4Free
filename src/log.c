#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/klog.h>

#include "c4f_log.h"
#include "c4f_sce.h"

static int g_logFd = -1;
static int g_klogEnabled = 1;

void c4fLogSetKlog(int enabled) { g_klogEnabled = enabled; }
int  c4fLogKlogEnabled(void) { return g_klogEnabled; }

void c4fLogOpen(void)
{
    (void)mkdir("/data", 0777);
    (void)mkdir(C4F_LOG_DIR, 0777);
    g_logFd = open(C4F_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    c4fLog("---- Control4Free spike log (stage %d) ----\n", C4F_STAGE);
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

    if (g_klogEnabled) klog_printf("[c4f] %s", msg);
    if (g_logFd >= 0) (void)write(g_logFd, msg, (size_t)n);
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
    (void)sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}
