/* Control4Free -- system-wide virtual controllers for the PS4.
 *
 * The browser payload builds on a successful hardware experiment: a GoldHEN
 * payload can create a virtual controller that the PS4 menus accept.
 * A game plugin cannot (GoldHEN only loads plugins into game processes, and the
 * menus live in SceShellUI/SceShellCore), so Control4Free is a payload and uses
 * Sony's own virtual-device API, the one Remote Play goes through.
 *
 * The work is split into stages so each call can be checked on klog before the
 * next one is let near the console. Pick one at build time:
 *
 *   C4F_STAGE=0  browser controller (default), with explicit creation/sign-in.
 *   C4F_STAGE=1  probe only: firmware, credentials, users. No scePad call.
 *   C4F_STAGE=2  plus libSceMbus, scePadSetProcessPrivilege and scePadInit.
 *   C4F_STAGE=3  plus AddDevice. Holds the device a few seconds so the
 *                "Who's using this controller?" screen can be watched for,
 *                then deletes it. No input is injected.
 *   C4F_STAGE=4  plus one short PS press, then 10 s of neutral input so a user
 *                selection prompt can show. No MBus bind (it crashed; C4F_BIND=1
 *                puts it back).
 *   C4F_STAGE=5  the sign-in attempt: raise the selection screen, press Cross to
 *                confirm the highlighted user, then check whether the pad was
 *                really assigned. Build it with C4F_VDA_USER=1.
 *   C4F_STAGE=6  button mapping: inject one bit at a time and read the pad back,
 *                so the map comes from the system, not from guessing. Build with
 *                the default user so the pad is assigned and readable.
 *   C4F_STAGE=7  visual direction test on the selection screen.
 *   C4F_STAGE=8  command server: stays resident on port 4264 and takes commands
 *                from dev\scripts\console.ps1, so the PayLoader is needed once.
 *
 * Diagnostic stages require an explicit build setting.
 */

#include <stdint.h>
#include <unistd.h>

#include <ps4/kernel.h>

#include "c4f_log.h"
#include "c4f_sce.h"
#include "c4f_vda.h"
#include "c4f_server.h"
#include "c4f_web.h"

#ifndef C4F_STAGE
#define C4F_STAGE 0
#endif

/* The authid SplashDown raises to before the VDA calls. */
#define C4F_AUTHID_VDA 0x3800000000010003L

/* Stage 3: how long the virtual device is left in place to be observed. */
#ifndef C4F_STAGE3_DWELL_MS
#define C4F_STAGE3_DWELL_MS 8000
#endif

/* Stage 4 press shape. PS is one of only two bits confirmed on this path. A
 * short press: holding PS for over a second opens the quick menu instead. */
#ifndef C4F_PRESS_BUTTONS
#define C4F_PRESS_BUTTONS C4F_VDI_BUTTON_PS
#endif
#ifndef C4F_PRESS_LABEL
#define C4F_PRESS_LABEL "PS"
#endif
#ifndef C4F_PRE_NEUTRAL_MS
#define C4F_PRE_NEUTRAL_MS 300
#endif
#ifndef C4F_PRESS_MS
#define C4F_PRESS_MS 200
#endif
/* Neutral samples keep flowing after the press, like a real pad that is still
 * switched on, so a "Who's using this controller?" prompt has time to appear
 * before the device is removed (removing it cancels the prompt). */
#ifndef C4F_POST_NEUTRAL_MS
#define C4F_POST_NEUTRAL_MS 10000
#endif

/* Stage 5 timings. The screen takes a moment to come up after device creation, and
 * the pad has to keep reporting the whole time or the system drops it. */
#ifndef C4F_S5_SCREEN_WAIT_MS
#define C4F_S5_SCREEN_WAIT_MS 4000
#endif
#ifndef C4F_S5_CROSS_MS
#define C4F_S5_CROSS_MS 120
#endif
#ifndef C4F_S5_SETTLE_MS
#define C4F_S5_SETTLE_MS 3000
#endif
/* How long the pad is kept alive after the attempt, so the result stays on
 * screen and a second probe can run. */
#ifndef C4F_S5_ALIVE_MS
#define C4F_S5_ALIVE_MS 15000
#endif

/* sceMbusBindDeviceWithUserId crashed the host process on 2026-10-04 (stage 4,
 * ScePartyDaemon, reason 0xb). It is also not what we want: the goal is the
 * system's own user selection. Off unless asked for. */
#ifndef C4F_BIND
#define C4F_BIND 0
#endif

/* Reports what privileges this process actually has. On a GoldHEN console the
 * payload may already be jailbroken, so the before/after pair tells us whether
 * raising them by hand is needed at all. */
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

static int c4fFinish(int klogFd, int status)
{
    if (klogFd >= 0) close(klogFd);
    c4fRestoreCredentials();
    c4fLog("exiting with status %d\n", status);
    c4fLogClose();
    return status;
}

int main(void)
{
    pid_t pid = getpid();
    uint32_t fw;
    int32_t userId;
    int klogFd = -1;
    int32_t ret;
#if C4F_STAGE >= 3 && C4F_STAGE <= 7
    C4fVirtualPad pad;
#endif

    c4fLogOpen();
#if C4F_STAGE != 0
    c4fNotify("Control4Free: stage %d", C4F_STAGE);
#endif

    fw = kernel_get_fw_version();
    c4fLog("pid=%d firmware=0x%08x (10.01 reads as 0x1001xxxx)\n", pid, fw);

    c4fLogCredentials("on entry", pid);
    if (c4fRaiseCredentials(pid) != 0) return c4fFinish(klogFd, 1);
    c4fLogCredentials("after raise", pid);

    /* Opened before anything creates a device, so the add shows up in the scan. */
    klogFd = c4fKlogOpen();
    c4fKlogDrain(klogFd);
    /* Stage 3 depends on reading kernel lines back, so prove that works first. */
    c4fKlogSelfTest(klogFd);

    ret = sceUserServiceInitialize(NULL);
    c4fLog("sceUserServiceInitialize = 0x%08x\n", (uint32_t)ret);

    userId = c4fPickUserId();
    c4fLog("chosen userId=0x%08x\n", (uint32_t)userId);

#if C4F_STAGE == 1
    c4fLog("stage 1 done: nothing in libScePad was called\n");
    c4fNotify("Control4Free: probe done");
#else
    /* libScePad has unresolved imports into libSceMbus in a payload, so MBus has
     * to be loaded first or the first scePad call can kill the process. */
    if (c4fMbusInit() != 0) {
        c4fNotify("Control4Free: libSceMbus missing");
        return c4fFinish(klogFd, 1);
    }
    if (c4fPadInit() != 0) {
        c4fNotify("Control4Free: scePadInit failed");
        return c4fFinish(klogFd, 1);
    }
    c4fLog("pad service ready\n");
#endif

#if C4F_STAGE == 8
    /* Resident command server: sent once, then driven over its own port. */
    if (c4fServerRun(userId, klogFd) != 0)
        return c4fFinish(klogFd, 1);
#endif

#if C4F_STAGE == 0
    /* c4fWebRun owns the klog descriptor from here on. */
    ret = c4fWebRun(klogFd);
    klogFd = -1;
    if (ret != 0) {
        c4fNotify("Control4Free: could not start; check payload log");
        return c4fFinish(klogFd, 1);
    }
#endif

#if C4F_STAGE >= 3 && C4F_STAGE <= 7
    if (c4fVirtualPadAdd(&pad, userId, klogFd) != 0) {
        c4fNotify("Control4Free: no virtual device");
        return c4fFinish(klogFd, 1);
    }
    /* The toast names the user value, so whoever watches the TV can tell runs apart. */
    c4fNotify("Control4Free: device 0x%x, user 0x%x",
              (uint32_t)pad.handle, (uint32_t)pad.vdaUserId);
#endif

#if C4F_STAGE == 3
    c4fLog("holding the device for %d ms -- watch the screen for a controller "
           "assignment prompt\n", C4F_STAGE3_DWELL_MS);
    usleep(C4F_STAGE3_DWELL_MS * 1000);
    c4fVirtualPadRemove(&pad);
    c4fNotify("Control4Free: device removed");
#endif

#if C4F_STAGE == 7
    /* Visual fallback for the button map, for when reading the pad back does not
     * work. It tests one hypothesis: that the VDA path uses the ordinary
     * documented layout (PS = 0x10000 fits it, which is why it is likely).
     * Build with C4F_VDA_USER=1 so the selection screen is what receives the
     * presses -- a d-pad there only moves a highlight.
     * Watch the screen and report what moved; nothing is confirmed here. */
    {
        ScePadData neutral;
        int press;

        c4fPadDataNeutral(&neutral);
        ret = c4fVirtualPadInsert(&pad, &neutral);
        c4fLog("first neutral InsertData = 0x%08x\n", (uint32_t)ret);

        c4fVirtualPadHold(&pad, 0, 500, "pre-neutral");
        /* AddDevice(user=1) opens selection itself. PS would cancel it. */
        c4fNotify("C4F: waiting for user selection");
        c4fVirtualPadHold(&pad, 0, 4000, "waiting for the screen");

        c4fNotify("C4F: 3x DOWN now");
        for (press = 0; press < 3; press++) {
            c4fVirtualPadHold(&pad, C4F_PAD_DOWN, 120, "DOWN 0x40");
            c4fVirtualPadHold(&pad, 0, 900, "gap");
        }
        c4fVirtualPadHold(&pad, 0, 1500, "pause");

        c4fNotify("C4F: 3x UP now");
        for (press = 0; press < 3; press++) {
            c4fVirtualPadHold(&pad, C4F_PAD_UP, 120, "UP 0x10");
            c4fVirtualPadHold(&pad, 0, 900, "gap");
        }

        c4fVirtualPadHold(&pad, 0, 4000, "staying alive");
        c4fVirtualPadRemove(&pad);
        c4fNotify("C4F: direction test finished");
    }
#endif

#if C4F_STAGE == 6
    /* Button mapping, read back from the system instead of watched on screen.
     * Build this one with the default user (0x10000000) so the pad is already
     * assigned and can be read; an unassigned pad answers nothing. */
    {
        int32_t readHandle;
        ScePadData neutral;

        c4fPadDataNeutral(&neutral);
        ret = c4fVirtualPadInsert(&pad, &neutral);
        c4fLog("first neutral InsertData = 0x%08x\n", (uint32_t)ret);

        /* The user the device was created for -- not the signed-in account,
         * whose handle would be the real DualShock. */
        readHandle = c4fOpenReadHandle(pad.vdaUserId);
        if (readHandle < 0) {
            c4fLog("no read handle: cannot map buttons this way\n");
            c4fNotify("Control4Free: no read handle");
        } else {
            c4fNotify("Control4Free: mapping buttons");
            c4fProbeButtonMap(&pad, readHandle);
            (void)scePadClose(readHandle);
        }
        c4fVirtualPadRemove(&pad);
        c4fNotify("Control4Free: probe finished");
    }
#endif

#if C4F_STAGE == 5
    c4fLog("no MBus bind: the system's own user selection is the point here\n");
    {
        ScePadData neutral;
        int32_t assigned;

        c4fPadDataNeutral(&neutral);
        ret = c4fVirtualPadInsert(&pad, &neutral);
        c4fLog("first neutral InsertData = 0x%08x\n", (uint32_t)ret);
        if (ret < 0) {
            c4fNotify("Control4Free: InsertData failed 0x%x", (uint32_t)ret);
            c4fVirtualPadRemove(&pad);
            return c4fFinish(klogFd, 1);
        }

        c4fLog("before anything: ");
        (void)c4fProbeAssignment(userId);

        /* AddDevice(user=1) opens selection itself. A PS press here cancels
         * selection and disconnects the pad from the sign-in flow. */
        c4fVirtualPadHold(&pad, 0, 500, "pre-neutral");
        c4fNotify("Control4Free: waiting for user selection");
        c4fVirtualPadHold(&pad, 0, C4F_S5_SCREEN_WAIT_MS, "waiting for the screen");

        /* Cross confirms whoever is highlighted. RIGHT=0x20 is also verified
         * on this path; stage 8 can use it to choose another user first. */
        c4fVirtualPadHold(&pad, C4F_VDI_BUTTON_CROSS, C4F_S5_CROSS_MS, "Cross press");
        c4fVirtualPadHold(&pad, 0, C4F_S5_SETTLE_MS, "settling");

        assigned = c4fProbeAssignment(userId);
        if (assigned != C4F_USER_ID_INVALID) {
            /* GetHandle may be the physical controller; it cannot prove that
             * this virtual DeviceId belongs to the returned user. */
            c4fLog("read handle found for user 0x%08x; verify this virtual "
                   "DeviceId's OWNER_CHANGED event in klog\n", (uint32_t)assigned);
        }
        c4fNotify("Control4Free: confirmation sent; check system log");

        /* Keep reporting so the result stays visible, then look once more. */
        c4fVirtualPadHold(&pad, 0, C4F_S5_ALIVE_MS, "staying alive");
        c4fLog("final check: ");
        (void)c4fProbeAssignment(userId);

        c4fVirtualPadRemove(&pad);
        c4fNotify("Control4Free: done");
    }
#endif

#if C4F_STAGE == 4
#if C4F_BIND
    if (c4fVirtualPadBind(&pad, userId) != 0)
        c4fLog("bind failed; injecting anyway to see what happens\n");
#else
    c4fLog("no MBus bind: leaving user selection to the system\n");
#endif

    {
        ScePadData neutral;
        c4fPadDataNeutral(&neutral);
        ret = c4fVirtualPadInsert(&pad, &neutral);
        c4fLog("first neutral InsertData = 0x%08x\n", (uint32_t)ret);
        if (ret < 0) {
            c4fNotify("Control4Free: InsertData failed 0x%x", (uint32_t)ret);
            c4fVirtualPadRemove(&pad);
            return c4fFinish(klogFd, 1);
        }
    }

    c4fNotify("Control4Free: pressing %s", C4F_PRESS_LABEL);
    c4fVirtualPadHold(&pad, 0, C4F_PRE_NEUTRAL_MS, "pre-neutral");
    c4fVirtualPadHold(&pad, C4F_PRESS_BUTTONS, C4F_PRESS_MS, C4F_PRESS_LABEL " press");
    c4fVirtualPadHold(&pad, 0, C4F_POST_NEUTRAL_MS, "post-neutral");

    c4fVirtualPadRemove(&pad);
    c4fNotify("Control4Free: done");
#endif

    return c4fFinish(klogFd, 0);
}
