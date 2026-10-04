/* Local HTTP/WebSocket integration harness. No console access. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "c4f_net.h"
#include "c4f_vda.h"
#include "c4f_web.h"

static unsigned nextId = 0x11030d;
static void (*waitFn)(void *);
static void *waitContext;
static int klogSource = -1;
void c4fLog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void c4fNotify(const char *fmt, ...) { (void)fmt; }
void c4fVdaSetWaitCallback(void (*fn)(void *), void *ctx) { waitFn = fn; waitContext = ctx; }
void c4fKlogDrain(int fd) { (void)fd; }
int c4fKlogSelfTest(int fd) { (void)fd; return getenv("C4F_TEST_KLOG_BUSY") ? -1 : 0; }
/* A klog source that only turns up later (AutoRun before GoldHEN's klog server). */
int c4fKlogOpenDevice(void)
{
    int fds[2];
    if (getenv("C4F_TEST_KLOG_BUSY")) { printf("KLOG none\n"); return -1; }
    if (klogSource >= 0) { int fd = dup(klogSource); printf("KLOG %d\n", fd); return fd; }
    if (!getenv("C4F_TEST_KLOG_LATE") || pipe(fds)) { printf("KLOG none\n"); return -1; }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    printf("KLOG %d\n", fds[0]);
    return fds[0];
}
int c4fVirtualPadAddAs(C4fVirtualPad *p, int32_t user, int32_t vdaUser, int fd)
{
    (void)fd;
    for (int i = 0; i < 3; i++) { if (waitFn) waitFn(waitContext); usleep(20000); }
    if (getenv("C4F_FAIL_ADD")) return -1;
    memset(p, 0, sizeof(*p));
    p->handle = nextId; p->deviceId = nextId; nextId += 0x10000;
    p->userId = user; p->vdaUserId = vdaUser; p->owned = 1;
    printf("ADD %x %d\n", p->handle, vdaUser);
    return 0;
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
    const char *fd = getenv("C4F_TEST_KLOG_FD");
    int logs[2];
    setvbuf(stdout, NULL, _IONBF, 0);
    if (fd) { klogSource = atoi(fd); return c4fWebRun(-1) != 0; }
    if (pipe(logs)) return 1;
    fcntl(logs[0], F_SETFL, O_NONBLOCK);
    klogSource = logs[0];
    int result = c4fWebRun(-1);
    close(logs[1]);
    return result != 0;
}
