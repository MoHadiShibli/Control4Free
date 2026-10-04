/* Control4Free -- system-wide virtual controllers for the PS4.
 *
 * A GoldHEN payload. A game plugin cannot reach the menus (GoldHEN loads
 * plugins into game processes only, and the menus live in
 * SceShellUI/SceShellCore), so Control4Free runs as a payload and uses Sony's
 * virtual-device API, the one Remote Play goes through. Phones and PCs drive
 * the controllers from a page the payload serves on port 4264 (web.c).
 *
 * The staged spike that established the call order is in git history, tag
 * spike-final.
 */

#include <fcntl.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps4/kernel.h>

#include "c4f_log.h"
#include "c4f_sce.h"
#include "c4f_vda.h"
#include "c4f_version.h"
#include "c4f_web.h"

/* The authid SplashDown raises to before the VDA calls. */
#define C4F_AUTHID_VDA 0x3800000000010003L

/* Logs the host process's privileges, before and after they are raised. */
static void c4fLogCredentials(const char *when, pid_t pid)
{
    uint8_t caps[16];
    uint64_t authid = kernel_get_ucred_authid(pid);
    int i;
    char hex[33];

    if (kernel_get_ucred_caps(pid, caps) == 0) {
        for (i = 0; i < 16; i++) {
            static const char digits[] = "0123456789abcdef";
            hex[i * 2] = digits[(caps[i] >> 4) & 0xf];
            hex[i * 2 + 1] = digits[caps[i] & 0xf];
        }
        hex[32] = '\0';
    } else {
        (void)__builtin_memcpy(hex, "<unreadable>", 13);
    }

    c4fLog("credentials %s: authid=0x%016llx caps=%s\n",
           when, (unsigned long long)authid, hex);
}

/* GoldHEN's PayLoader does not give us a process of our own: it runs the ELF
 * inside an existing system process (ScePartyDaemon on our console). So the
 * credentials we raise belong to that daemon, and they have to be put back
 * before we leave or the daemon carries on with our identity. */
static pid_t    g_pid;
static uint64_t g_savedAuthid;
static uint8_t  g_savedCaps[16];
static int      g_credsSaved;
static int      g_instanceFd = -1;

static int c4fRaiseCredentials(pid_t pid)
{
    uint8_t caps[16];
    int i;

    g_pid = pid;
    g_savedAuthid = kernel_get_ucred_authid(pid);
    g_credsSaved = kernel_get_ucred_caps(pid, g_savedCaps) == 0;
    if (!g_credsSaved || !g_savedAuthid) {
        g_credsSaved = 0;
        c4fLog("could not save host credentials; refusing to modify them\n");
        return -1;
    }

    for (i = 0; i < 16; i++) caps[i] = 0xff;

    if (kernel_set_ucred_authid(pid, C4F_AUTHID_VDA) != 0) {
        c4fLog("kernel_set_ucred_authid failed\n");
        return -1;
    }
    if (kernel_set_ucred_caps(pid, caps) != 0) {
        c4fLog("kernel_set_ucred_caps failed\n");
        return -1;
    }
    return 0;
}

static void c4fRestoreCredentials(void)
{
    if (!g_credsSaved) {
        c4fLog("credentials were not saved; leaving them as they are\n");
        return;
    }
    if (kernel_set_ucred_authid(g_pid, g_savedAuthid) != 0)
        c4fLog("restoring authid failed\n");
    if (kernel_set_ucred_caps(g_pid, g_savedCaps) != 0)
        c4fLog("restoring caps failed\n");
    c4fLogCredentials("restored", g_pid);
}

static int c4fFinish(int status)
{
    c4fRestoreCredentials();
    c4fLog("exiting with status %d\n", status);
    c4fLogClose();
    if (g_instanceFd >= 0) close(g_instanceFd);
    return status;
}

int main(void)
{
    pid_t pid = getpid();
    int32_t ret;

    /* One copy at a time. A second load (AutoRun plus the launcher, say) must
     * not change the shared host's credentials or rotate the first one's log.
     * The lock belongs to this open descriptor, even inside the same process,
     * and the kernel drops it with the descriptor. */
    (void)mkdir(C4F_LOG_DIR, 0777);
    g_instanceFd = open(C4F_LOG_DIR "/instance.lock", O_WRONLY | O_CREAT, 0666);
    if (g_instanceFd < 0 || flock(g_instanceFd, LOCK_EX | LOCK_NB)) {
        if (g_instanceFd >= 0) close(g_instanceFd);
        c4fNotify("Control4Free is already running");
        return 1;
    }
    c4fLogOpen();
    c4fLog("Control4Free %s: pid=%d firmware=0x%08x\n", C4F_VERSION, pid, kernel_get_fw_version());

    c4fLogCredentials("on entry", pid);
    if (c4fRaiseCredentials(pid) != 0) return c4fFinish(1);
    c4fLogCredentials("after raise", pid);

    ret = sceUserServiceInitialize(NULL);
    c4fLog("sceUserServiceInitialize = 0x%08x\n", (uint32_t)ret);

    /* libScePad has unresolved imports into libSceMbus in a payload, so MBus has
     * to be loaded first or the first scePad call can kill the process. */
    if (c4fMbusInit() != 0) {
        c4fNotify("Control4Free: libSceMbus missing");
        return c4fFinish(1);
    }
    if (c4fPadInit() != 0) {
        c4fNotify("Control4Free: scePadInit failed");
        return c4fFinish(1);
    }

    /* The service takes the kernel log only while a controller signs in. */
    ret = c4fWebRun(-1);
    if (ret != 0) c4fNotify("Control4Free could not start; see /data/control4free/control4free.log");
    return c4fFinish(ret != 0);
}
