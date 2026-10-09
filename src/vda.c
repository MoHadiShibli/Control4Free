/* Control4Free -- virtual device creation and input injection.
 *
 * Ported from seregonwar/SplashDown's psbutton.c (GPL-3.0), the only known
 * working caller of this API on a PS4. The call order and the klog trick for
 * recovering the device handle both come from there.
 *
 * Nothing here blocks. Adding a device takes two steps, because the handle only
 * turns up in the kernel log a moment after the call: the service issues
 * c4fVirtualPadAdd, keeps serving everyone else while it reads the log, and
 * calls c4fVirtualPadAdopt once the line arrives.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ps4/klog.h>

#include "c4f_log.h"
#include "c4f_sce.h"
#include "c4f_vda.h"

#ifndef RTLD_NOW
#define RTLD_NOW 2
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0x100
#endif

/* ---- libSceMbus ---- */

int c4fMbusInit(void)
{
    static const char *paths[] = {
        C4F_LIBSCEMBUS_PATH,
        "/system/common/lib/libSceMbus",
        "libSceMbus.sprx",
        "libSceMbus",
    };
    void *handle = NULL;
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]) && !handle; i++) {
        handle = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL);
        c4fLog("dlopen(%s) = %p\n", paths[i], handle);
    }
    if (!handle) {
        c4fLog("FATAL: libSceMbus did not load; not touching libScePad\n");
        return -1;
    }
    return 0;
}

/* ---- pad service ---- */

/* scePadInit has to come first. SplashDown sets the privilege before init and
 * never checks the result; on our console that call returns 0x80920005
 * (SCE_PAD_ERROR_NOT_INITIALIZED), so it did nothing. Ghostcontrol only sets it
 * in processes whose libScePad is already initialized. */
int c4fPadInit(void)
{
    int32_t ret;

    ret = scePadInit();
    c4fLog("scePadInit() = 0x%08x\n", (uint32_t)ret);
    if (ret != 0) {
        c4fLog("scePadInit failed\n");
        return -1;
    }

    ret = scePadSetProcessPrivilege(1);
    c4fLog("scePadSetProcessPrivilege(1) = 0x%08x\n", (uint32_t)ret);
    return 0;
}

/* ---- the kernel log ----
 *
 * AddDevice hands back a status, not a handle. The handle we need is the MBus
 * DeviceId the kernel logs right after the call, and reading the log is the only
 * way for a payload to learn it. Ugly, but it is what works.
 *
 * /dev/klog has a single reader. GoldHEN's klog server opens it only while it
 * has a client and serves one client at a time; after a client leaves it can go
 * minutes without serving the next one (console, 2026-10-04). So the service
 * reads the device itself, and only while a controller signs in.
 */

int c4fKlogOpenDevice(void)
{
    int fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        c4fLog("open(/dev/klog) failed errno=%d (16: GoldHEN's klog server has a client)\n", errno);
        return -1;
    }
    c4fLog("reading klog through /dev/klog\n");
    return fd;
}

/* A line of our own, to prove the reader really delivers. It deliberately has
 * no [c4f] prefix: the reader skips those, because our own mirrored log would
 * otherwise come back round and feed itself. */
void c4fKlogMark(const char *marker)
{
    klog_printf("%s\n", marker);
}

/* ---- device lifecycle ---- */

int32_t c4fVirtualPadAdd(int32_t vdaUser)
{
    C4fVdaParam param;
    int32_t ret;
    int i;

    (void)memset(&param, 0, sizeof(param));
    param.size = (int32_t)sizeof(param);
    param.userId = vdaUser;
    /* A marker, so the log shows whether the call writes anything back. It never
     * has on this console, which is why the DeviceId has to come from klog. */
    for (i = 0; i < 6; i++) param.pad[i] = (int32_t)0xdeadbeef;

    c4fLog("AddDevice: size=%d userId=0x%08x type=%d\n",
           param.size, (uint32_t)param.userId, C4F_VIRTUAL_DEVICE_TYPE);
    ret = scePadVirtualDeviceAddDevice(&param, C4F_VIRTUAL_DEVICE_TYPE);
    c4fLog("AddDevice = 0x%08x pad=[0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x]\n",
           (uint32_t)ret,
           (uint32_t)param.pad[0], (uint32_t)param.pad[1], (uint32_t)param.pad[2],
           (uint32_t)param.pad[3], (uint32_t)param.pad[4], (uint32_t)param.pad[5]);
    /* A non-zero return does not have to mean failure: on PS5 SplashDown sees
     * 0x803b0006 and the device is created anyway. So the caller waits for the
     * log line either way. */
    return ret;
}

void c4fVirtualPadAdopt(C4fVirtualPad *out, int32_t userId, int32_t vdaUser, uint64_t deviceId)
{
    (void)memset(out, 0, sizeof(*out));
    out->deviceId = deviceId;
    out->handle = (int32_t)(deviceId & 0xffffffffu);
    out->userId = userId;
    out->vdaUserId = vdaUser;
    out->owned = 1;
    c4fLog("DeviceId=0x%llx handle=0x%08x\n",
           (unsigned long long)deviceId, (uint32_t)out->handle);
}

int32_t c4fVirtualPadFeedback(const C4fVirtualPad *pad, C4fPadFeedback *out)
{
    uint8_t buf[256];   /* the call's size is unknown; it has only ever used 17 */

    (void)memset(out, 0, sizeof(*out));
    if (!scePadVirtualDeviceGetRemoteSetting) return -1;
    (void)memset(buf, 0, sizeof(buf));
    int32_t ret = scePadVirtualDeviceGetRemoteSetting(pad->handle, buf);
    if (ret == 0) c4fPadFeedbackParse(buf, sizeof(buf), out);
    return ret;
}

int32_t c4fUserName(uint32_t userId, char *out, size_t size)
{
    char name[64];   /* the system's own limit is 16 characters */

    if (size) out[0] = 0;
    (void)memset(name, 0, sizeof(name));
    int32_t ret = sceUserServiceGetUserName((int32_t)userId, name, sizeof(name) - 1);
    if (ret == 0) snprintf(out, size, "%s", name);
    return ret;
}

int32_t c4fLoginUsers(int32_t userIds[4])
{
    C4fUserServiceLoginList list = {{-1, -1, -1, -1}};
    for (int i = 0; i < 4; i++) userIds[i] = C4F_USER_ID_INVALID;
    int32_t ret = sceUserServiceGetLoginUserIdList(&list);
    if (ret == 0) memcpy(userIds, list.userId, sizeof(list.userId));
    return ret;
}

int32_t c4fUserEvent(int32_t *type, uint32_t *userId)
{
    C4fUserServiceEvent event = {-1, C4F_USER_ID_INVALID};
    int32_t ret = sceUserServiceGetEvent(&event);
    if (ret == 0) { *type = event.event; *userId = (uint32_t)event.userId; }
    return ret;
}

void c4fPadDataNeutral(ScePadData *data)
{
    (void)memset(data, 0, sizeof(*data));
    data->lx = 128;
    data->ly = 128;
    data->rx = 128;
    data->ry = 128;
    data->quat[3] = 1.0f;   /* identity rotation */
    data->connected = 1;
    data->count = 1;
}

/* A real pad stamps every report with a rising time and a rising counter. Ours
 * sent the same zero every frame, which anything that de-duplicates reports
 * would see as "no new data". Give each sample its own stamp. */
static uint64_t g_sampleStamp;
static uint8_t  g_sampleCount;

int32_t c4fVirtualPadInsert(const C4fVirtualPad *pad, const ScePadData *data)
{
    ScePadData stamped = *data;

    g_sampleStamp += 33 * 1000u;   /* microseconds, like a real pad */
    g_sampleCount++;
    stamped.timestamp = g_sampleStamp;
    stamped.count = g_sampleCount;

    return scePadVirtualDeviceInsertData(pad->handle, &stamped);
}

void c4fVirtualPadRemove(C4fVirtualPad *pad)
{
    int32_t ret;

    if (!pad->owned || pad->handle < 0) return;

    /* DeleteDevice alone, as SplashDown does. sceMbusDisconnectDevice exists, but
     * calling it before the delete has not been tried on hardware, so it stays
     * out of the cleanup path until it has. */
    ret = scePadVirtualDeviceDeleteDevice(pad->handle);
    c4fLog("DeleteDevice(0x%08x) = 0x%08x\n", (uint32_t)pad->handle, (uint32_t)ret);

    pad->owned = 0;
    pad->handle = -1;
    pad->deviceId = 0;
}
