/* Real diagnostic storage code; no console or kernel-log reader is contacted. */
#define C4F_DIAG_DIR "/tmp/c4f-diag-host-test"
#define C4F_DIAG 1
#include "../launcher/diag.c"
#include <assert.h>

static int fault, calls;
ssize_t __real_write(int fd, const void *buf, size_t n);
int __real_fsync(int fd);
ssize_t __wrap_write(int fd, const void *buf, size_t n)
{
    if (fault == 1) { errno = ENOSPC; return -1; }
    if (fault == 2) return 0;
    if (fault == 3 && calls++ == 0) { errno = EINTR; return -1; }
    return __real_write(fd, buf, n > 3 ? 3 : n);
}
int __wrap_fsync(int fd) { if (fault == 4) { errno = EIO; return -1; } return __real_fsync(fd); }
int sceKernelGetSystemSwVersion(OrbisKernelSwVersion *version)
{ snprintf(version->VersionString, sizeof(version->VersionString), "host test"); return 0; }
long c4fGoldHenCommand(unsigned long command, void *data) { (void)command; (void)data; return 0; }
const unsigned char *c4fAutorunBundled(size_t *size) { *size = 0; return NULL; }
uint64_t c4fLauncherTimeMs(void) { return 1000; }

int main(void)
{
    assert(access(C4F_DIAG_DIR, F_OK) != 0);
    c4fDiagStart(0, 0); c4fDiagStop();
    struct stat info; assert(!stat(C4F_DIAG_DIR, &info) && S_ISDIR(info.st_mode));
    fault = 3; calls = 0;
    assert(!c4fWriteAll(C4F_DIAG_REPORT, "complete write", 0));
    char *text = c4fReadTail(C4F_DIAG_REPORT, 100, NULL); assert(text && !strcmp(text, "complete write")); free(text);
    for (fault = 1; fault <= 4; fault++) {
        if (fault == 3) continue;
        assert(c4fWriteAll(C4F_DIAG_REPORT, "failure", 0) == -1);
        c4fSaveReport(); assert(strstr(c4fReportNote, "Internal save failed"));
        assert(!strstr(c4fReportNote, "Saved /data"));
    }
    fault = 0; c4fSaveReport(); assert(strstr(c4fReportNote, "Saved /data"));
    assert(!c4fWriteAll(C4F_DIAG_LOG ".1", "older segment\n", 0));
    assert(!c4fWriteAll(C4F_DIAG_LOG, "latest segment\n", 0));
    text = c4fCurrentLogPage(); assert(strstr(text, "older segment") && strstr(text, "latest segment")); free(text);
    unlink(C4F_DIAG_KLOG); unlink(C4F_DIAG_REPORT); unlink(C4F_DIAG_LOG); unlink(C4F_DIAG_LOG ".1"); rmdir(C4F_DIAG_DIR);
    puts("PASS independent diagnostic directory, EINTR/short writes, ENOSPC/zero/fsync failures and truthful report results");
}
