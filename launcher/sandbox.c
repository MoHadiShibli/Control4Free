/* Leaving the app sandbox through GoldHEN. */
#include "sandbox.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int c4fOut;

int c4fSandboxIsOut(void) { return c4fOut; }

#ifdef __FreeBSD__
/* GoldHEN's SDK call (syscall 500), command 2: jailbreak this process. The
 * kernel stores the old credentials in this layout, the GoldHEN Plugins SDK's
 * struct jailbreak_backup (MIT). The app never goes back, so it is kept only
 * because GoldHEN writes into it. */
typedef struct {
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid, cr_caps[2];
    void *cr_prison, *fd_cdir, *fd_jdir, *fd_rdir;
} C4fJailbreak;

static long c4fGoldHen(uint64_t command, void *data)
{
    long ret = 500;
    int failed;
    __asm__ volatile("syscall" : "+a"(ret), "=@ccc"(failed) : "D"(command), "S"(data)
                     : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
    return failed ? -ret : ret;
}

int c4fSandboxLeave(void)
{
    static C4fJailbreak backup;
    if (c4fOut) return 0;
    /* Without GoldHEN the call does not exist: get an error, not SIGSYS. The
     * kernel's SIGSYS is FreeBSD's 12; OpenOrbis's header has Linux's 31. */
    signal(12, SIG_IGN);
    memset(&backup, 0, sizeof(backup));
    long ret = c4fGoldHen(2, &backup);
    if (ret != 0) { errno = ret < 0 ? (int)-ret : EPERM; return -1; }
    c4fOut = 1;
    return 0;
}
#else
/* Host tests: there is no sandbox. */
int c4fSandboxLeave(void)
{
    if (getenv("C4F_TEST_NO_GOLDHEN")) { errno = ENOSYS; return -1; }
    c4fOut = 1;
    return 0;
}
#endif
