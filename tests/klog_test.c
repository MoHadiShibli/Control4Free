/* The real kernel-log opener, with only /dev/klog and the klog writer stubbed.
 * Modes: direct (the device is free), busy (another reader has it).
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
static int device[2] = {-1, -1}, deviceBusy, deviceOpens;
void c4fLog(const char *fmt, ...) { (void)fmt; }

/* Stands in for the kernel log's writer: what we write comes back to a reader. */
int klog_printf(const char *fmt, ...)
{
    char line[1024]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    if (device[1] >= 0) assert(write(device[1], line, (size_t)n) == n);
    return n;
}
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (strcmp(path, "/dev/klog")) return __real_open(path, flags);
    deviceOpens++;
    if (deviceBusy) { errno = EBUSY; return -1; }
    assert(pipe(device) == 0);
    assert(fcntl(device[0], F_SETFL, O_NONBLOCK) == 0);
    return device[0];
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

    fd = c4fKlogOpenDevice();
    if (!deviceBusy) {
        char buf[256] = {0};
        assert(fd >= 0 && (fcntl(fd, F_GETFL) & O_NONBLOCK) && deviceOpens == 1);
        /* A marker written through klog comes back to the reader, and carries no
         * [c4f] prefix, which is how the service tells it from its own output. */
        c4fKlogMark("c4f-klog-mark-1");
        assert(read(fd, buf, sizeof(buf) - 1) > 0);
        assert(strstr(buf, "c4f-klog-mark-1") && !strstr(buf, "[c4f]"));
        close(fd);
    } else {
        assert(fd == -1 && deviceOpens == 1);
    }
    usleep(50000);
    assert(peer < 0); /* GoldHEN's klog stream was never touched */

    pthread_cancel(thread);
    pthread_join(thread, NULL);
    close(listener);
    printf("PASS real klog acquisition: %s\n", mode);
}
