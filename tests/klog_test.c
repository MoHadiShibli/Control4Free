/* The real VDA capture functions, with only /dev/klog and the klog writer
 * stubbed. Modes: direct (the device is free), busy (another reader has it).
 *
 * GoldHEN's klog server on port 3232 serves one client at a time and can go
 * minutes without serving the next one, so the service must never reach for it.
 * A listener on 3232 stays open through both modes and must never see a
 * connection. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "c4f_vda.h"

static atomic_int peer = -1;
static int direct[2] = {-1, -1}, enabled = 1, deviceBusy, directOpens, waits;
static void waitCallback(void *unused) { (void)unused; waits++; }
void c4fLog(const char *fmt, ...) { (void)fmt; }
int c4fLogKlogEnabled(void) { return enabled; }
void c4fLogSetKlog(int value) { enabled = value; }
int klog_printf(const char *fmt, ...)
{
    char line[1024]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    if (direct[1] >= 0) { assert(write(direct[1], line, n) == n); return n; }
    return n;
}
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (strcmp(path, "/dev/klog")) return __real_open(path, flags);
    directOpens++;
    if (deviceBusy) { errno = EBUSY; return -1; }
    assert(pipe(direct) == 0);
    assert(fcntl(direct[0], F_SETFL, O_NONBLOCK) == 0);
    return direct[0];
}
static void *serve(void *arg)
{
    int listener = *(int *)arg;
    peer = accept(listener, NULL, NULL);
    return NULL;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *mode = argv[1];
    int listener, fd; pthread_t thread;
    deviceBusy = strcmp(mode, "direct") != 0;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(3232), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(listener, 1) == 0);
    assert(pthread_create(&thread, NULL, serve, &listener) == 0);
    c4fVdaSetWaitCallback(waitCallback, NULL);

    fd = c4fKlogOpenDevice();
    if (!deviceBusy) {
        /* Non-blocking, opened once, and verified by a marker read back, which
         * the wait callback keeps existing controllers reporting through. */
        assert(fd >= 0 && (fcntl(fd, F_GETFL) & O_NONBLOCK) && directOpens == 1 && waits > 0);
        close(fd);
    } else {
        assert(fd == -1 && directOpens == 1);
    }
    usleep(50000);
    assert(peer < 0); /* GoldHEN's klog stream was never touched */
    assert(enabled == 1);

    pthread_cancel(thread);
    pthread_join(thread, NULL);
    close(listener);
    printf("PASS real klog acquisition: %s\n", mode);
}
