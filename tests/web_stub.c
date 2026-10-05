/* Local HTTP/WebSocket integration harness. No console access.
 *
 * The PS4 side is a fake kernel log: a pipe the service reads like /dev/klog,
 * that this stub writes the same lines into that firmware 10.01 writes, our own
 * mirrored [c4f] output included. So the real service does the real reading,
 * marker verification and DeviceId matching, and only scePad is absent.
 *
 * A test injects its own kernel lines by writing them to the pipe named in
 * C4F_TEST_KLOG_FD; they are relayed into the log. The line C4F-TEST-CLOSE-KLOG
 * kills the log instead, as GoldHEN taking the device would.
 *
 * Environment switches:
 *   C4F_TEST_KLOG_FD     read end of the test's injection pipe
 *   C4F_TEST_KLOG_BUSY   /dev/klog cannot be opened: another reader has it
 *   C4F_TEST_KLOG_DEAD   it opens, but nothing is ever delivered
 *   C4F_TEST_ADD_DELAY   milliseconds before the device-added line turns up
 *   C4F_FAIL_ADD         AddDevice runs but its line never arrives
 *   C4F_FAIL_INPUT       InsertData fails while a button is held
 *   C4F_TEST_SLOW_MS     milliseconds each new device takes to adopt, the way
 *                        real console work spends time inside one loop iteration
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "c4f_net.h"
#include "c4f_vda.h"
#include "c4f_web.h"

static unsigned nextId = 0x11030d;
static int logRead = -1, logWrite = -1;
static pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;

/* The write end is non-blocking: a log nobody drains drops lines instead of
 * stalling the single-threaded service that reads it. */
static void logLine(const char *line)
{
    pthread_mutex_lock(&logLock);
    if (logWrite >= 0 && !getenv("C4F_TEST_KLOG_DEAD")) (void)!write(logWrite, line, strlen(line));
    pthread_mutex_unlock(&logLock);
}

/* Goes to the test's trace, and into the log as klog_printf would, so the
 * service has to recognise and skip its own lines. */
void c4fLog(const char *fmt, ...)
{
    char msg[1200], line[1232];
    va_list ap;
    va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    fputs(msg, stdout);
    snprintf(line, sizeof(line), "[c4f] %s", msg);
    if (!strchr(line, '\n')) strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    logLine(line);
}
void c4fNotify(const char *fmt, ...) { (void)fmt; }

/* Relays the test's injected lines into the log, and kills the log when the test
 * asks for it. One thread for the whole run. */
static void *relay(void *arg)
{
    int source = *(int *)arg;
    char buf[1024];
    /* The harness hands it over non-blocking; this thread can afford to wait. */
    fcntl(source, F_SETFL, fcntl(source, F_GETFL) & ~O_NONBLOCK);
    for (;;) {
        ssize_t n = read(source, buf, sizeof(buf) - 1);
        if (n <= 0) return NULL;
        buf[n] = 0;
        if (strstr(buf, "C4F-TEST-CLOSE-KLOG")) {
            pthread_mutex_lock(&logLock);
            if (logWrite >= 0) { close(logWrite); logWrite = -1; }
            pthread_mutex_unlock(&logLock);
            continue;
        }
        logLine(buf);
    }
}

int c4fKlogOpenDevice(void)
{
    int fds[2], fd;
    if (getenv("C4F_TEST_KLOG_BUSY")) { printf("KLOG none\n"); return -1; }
    pthread_mutex_lock(&logLock);
    if (logWrite < 0) {
        /* A fresh device, the way reopening /dev/klog gives one. */
        if (logRead >= 0) close(logRead);
        logRead = -1;
        if (!pipe(fds)) {
            fcntl(fds[0], F_SETFL, O_NONBLOCK);
            fcntl(fds[1], F_SETFL, O_NONBLOCK);
            logRead = fds[0]; logWrite = fds[1];
        }
    }
    fd = logRead >= 0 ? dup(logRead) : -1;
    pthread_mutex_unlock(&logLock);
    if (fd < 0) { printf("KLOG none\n"); return -1; }
    printf("KLOG %d\n", fd);
    return fd;
}

void c4fKlogMark(const char *marker)
{
    char line[128];
    snprintf(line, sizeof(line), "%s\n", marker);
    logLine(line);
}

/* The line the login manager produces for a new virtual pad, after an optional
 * delay, so a test can hold a creation open and check the service stays usable. */
static void *announce(void *arg)
{
    char line[160];
    unsigned handle = (unsigned)(uintptr_t)arg;
    const char *delay = getenv("C4F_TEST_ADD_DELAY");
    if (delay) usleep((unsigned)atoi(delay) * 1000);
    snprintf(line, sizeof(line), "<118>#LOGIN MGR# Receive Event : "
             "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x%x][type:1][subType:2]\n", handle);
    logLine(line);
    return NULL;
}

int32_t c4fVirtualPadAdd(int32_t vdaUser)
{
    pthread_t thread;
    unsigned handle = nextId;
    nextId += 0x10000;
    printf("ADD %x %d\n", handle, vdaUser);
    if (getenv("C4F_FAIL_ADD")) return 0;   /* created, never announced */
    if (getenv("C4F_TEST_ADD_DELAY") &&
        pthread_create(&thread, NULL, announce, (void *)(uintptr_t)handle) == 0)
        pthread_detach(thread);
    else
        announce((void *)(uintptr_t)handle);
    return 0;
}

void c4fVirtualPadAdopt(C4fVirtualPad *p, int32_t user, int32_t vdaUser, uint64_t deviceId)
{
    const char *slow = getenv("C4F_TEST_SLOW_MS");
    if (slow) usleep((unsigned)atoi(slow) * 1000);
    memset(p, 0, sizeof(*p));
    p->deviceId = deviceId; p->handle = (int32_t)(deviceId & 0xffffffffu);
    p->userId = user; p->vdaUserId = vdaUser; p->owned = 1;
    printf("ADOPT %x %d\n", (unsigned)p->handle, vdaUser);
}

void c4fVirtualPadRemove(C4fVirtualPad *p) { printf("REMOVE %x\n", p->handle); p->owned = 0; }
void c4fPadDataNeutral(ScePadData *p)
{
    memset(p, 0, sizeof(*p)); p->lx=p->ly=p->rx=p->ry=128; p->connected=1; p->quat[3]=1;
}
int32_t c4fVirtualPadInsert(const C4fVirtualPad *p, const ScePadData *d)
{
    printf("FRAME %llu %x %u %u %u %u %u %u %u %u\n", (unsigned long long)c4fTimeMs(),
           p->handle, d->buttons, d->lx, d->ly, d->rx, d->ry, d->l2, d->r2, d->touchData.fingers);
    return getenv("C4F_FAIL_INPUT") && d->buttons ? -5 : 0;
}

int main(void)
{
    static int source;
    const char *fd = getenv("C4F_TEST_KLOG_FD");
    pthread_t thread;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (fd && (source = atoi(fd)) >= 0 &&
        pthread_create(&thread, NULL, relay, &source) == 0)
        pthread_detach(thread);
    /* As the payload does: no reader until a controller needs one. */
    return c4fWebRun(-1) != 0;
}
