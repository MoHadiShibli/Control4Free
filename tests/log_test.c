#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "c4f_log.h"

static unsigned ticks;
int __wrap_clock_gettime(clockid_t id, struct timespec *out)
{
    assert(id == CLOCK_MONOTONIC);
    out->tv_sec = 4000 + ticks; out->tv_nsec = ticks++ ? 234000000 : 0;
    return 0;
}
int klog_printf(const char *fmt, ...) { (void)fmt; return 0; }
int sceKernelSendNotificationRequest(int a, void *b, size_t c, int d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
int main(void)
{
    char data[8192], large[2000];
    c4fLogOpen(); c4fLog("first\nsecond\n");
    memset(large, 'x', sizeof(large)-1); large[sizeof(large)-1] = 0;
    c4fLog("%s", large); c4fLogClose();
    FILE *file = fopen(C4F_LOG_PATH, "r"); assert(file);
    size_t n = fread(data, 1, sizeof(data)-1, file); data[n] = 0; fclose(file);
    assert(strstr(data, "[c4f] [+00:00:02.234] first\n"));
    assert(strstr(data, "[c4f] [+00:00:02.234] second\n"));
    char *p = data;
    while (*p) { assert(strncmp(p, "[c4f] [+", 8) == 0); p = strchr(p, '\n'); assert(p); p++; }
    c4fLogOpen(); c4fLog("new run\n"); c4fLogClose();
    file = fopen(C4F_LOG_PATH ".previous", "r"); assert(file);
    char previous[8192]; size_t m = fread(previous, 1, sizeof(previous), file); fclose(file);
    assert(m == n && memcmp(data, previous, n) == 0);
    c4fLogOpen();
    for (int i = 0; i < 2400; i++) c4fLog("%s", large);
    c4fLogClose();
    struct stat active, rollover;
    assert(!stat(C4F_LOG_PATH, &active) && !stat(C4F_LOG_PATH ".1", &rollover));
    assert(active.st_size <= 1024 * 1024 && rollover.st_size <= 1024 * 1024);
    assert(active.st_size > 0 && rollover.st_size > 0);
    c4fLogOpen(); c4fLogClose();
    assert(access(C4F_LOG_PATH ".1", F_OK) != 0);
    puts("PASS bounded active/rollover segments and stale rollover removal on restart");
    puts("PASS elapsed timestamps, multiline/truncated messages and previous-log preservation");
}
