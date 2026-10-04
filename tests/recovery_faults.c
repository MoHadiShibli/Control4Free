/* Link-time syscall fault injection; never linked into the payload. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int fault(const char *wanted)
{
    const char *path = getenv("C4F_TEST_FAULT");
    char value[32] = {0};
    FILE *file = path ? fopen(path, "r") : NULL;
    if (!file) return 0;
    (void)fgets(value, sizeof(value), file); fclose(file);
    if (strcmp(value, wanted)) return 0;
    unlink(path); errno = ENETDOWN; return 1;
}
int __real_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int __wrap_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
    if (fault("select")) { errno = EBADF; return -1; }
    return __real_select(n, r, w, e, t);
}
int __real_accept(int, struct sockaddr *, socklen_t *);
int __wrap_accept(int fd, struct sockaddr *a, socklen_t *n)
{
    if (fault("accept")) return -1;
    return __real_accept(fd, a, n);
}
int __real_getsockopt(int, int, int, void *, socklen_t *);
int __wrap_getsockopt(int fd, int level, int opt, void *out, socklen_t *size)
{
    if (opt == SO_ERROR && fault("socket")) { *(int *)out = ECONNRESET; return 0; }
    return __real_getsockopt(fd, level, opt, out, size);
}
