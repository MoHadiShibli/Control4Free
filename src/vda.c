/* Control4Free -- virtual device creation and input injection.
 *
 * Ported from seregonwar/SplashDown's psbutton.c (GPL-3.0), the only known
 * working caller of this API on a PS4. The call order and the klog trick for
 * recovering the device handle both come from there; see dev/notes/vda.md.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

#include <ps4/kernel.h>
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

static C4fMbusBindFn       g_mbusBind;
static C4fMbusDisconnectFn g_mbusDisconnect;
static void               *g_mbusHandle;
static void (*g_waitCallback)(void *);
static void *g_waitContext;

void c4fVdaSetWaitCallback(void (*callback)(void *), void *context)
{
    g_waitCallback = callback;
    g_waitContext = context;
}

static uint64_t c4fVdaNowMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

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

    g_mbusBind = (C4fMbusBindFn)dlsym(handle, "sceMbusBindDeviceWithUserId");
    g_mbusDisconnect = (C4fMbusDisconnectFn)dlsym(handle, "sceMbusDisconnectDevice");
    c4fLog("sceMbusBindDeviceWithUserId=%p sceMbusDisconnectDevice=%p\n",
           (void *)g_mbusBind, (void *)g_mbusDisconnect);

    if (!g_mbusBind) {
        c4fLog("FATAL: sceMbusBindDeviceWithUserId unresolved\n");
        return -1;
    }
    g_mbusHandle = handle;
    return 0;
}

/* libSceMbus is a client of a system service. Our host process (ScePartyDaemon)
 * never starts that client, and a bind without it faulted twice on 2026-10-04.
 * libScePad has its own wrapper for starting it, scePadMbusInit, which games
 * never call; there is also sceMbusInit itself. Neither has a published
 * signature, so both are called with no arguments and the return is logged.
 * which: 0 = scePadMbusInit, 1 = sceMbusInit. */
typedef int32_t (*C4fNoArgFn)(void);

int c4fMbusClientInit(int which, int32_t *outRet)
{
    const char *name = which == 0 ? "scePadMbusInit" : "sceMbusInit";
    void *lib;
    C4fNoArgFn fn;

    if (which == 0) {
        lib = dlopen("/system/common/lib/libScePad.sprx", RTLD_NOW | RTLD_GLOBAL);
    } else {
        lib = g_mbusHandle;
    }
    if (!lib) {
        c4fLog("%s: library handle missing\n", name);
        return -1;
    }

    fn = (C4fNoArgFn)dlsym(lib, name);
    c4fLog("%s resolved at %p\n", name, (void *)fn);
    if (!fn) return -1;

    *outRet = fn();
    c4fLog("%s() = 0x%08x\n", name, (uint32_t)*outRet);
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

static int c4fIsRealUser(int32_t userId)
{
    return userId != 0 && userId != C4F_USER_ID_INVALID;
}

int32_t c4fPickUserId(void)
{
    int32_t userId = C4F_USER_ID_INVALID;
    int32_t ret;

    if (sceUserServiceGetForegroundUser) {
        ret = sceUserServiceGetForegroundUser(&userId);
        c4fLog("GetForegroundUser = 0x%08x id=0x%08x\n", (uint32_t)ret, (uint32_t)userId);
        if (ret == 0 && c4fIsRealUser(userId)) return userId;
    } else {
        c4fLog("GetForegroundUser not present on this firmware\n");
    }

    userId = C4F_USER_ID_INVALID;
    ret = sceUserServiceGetInitialUser(&userId);
    c4fLog("GetInitialUser = 0x%08x id=0x%08x\n", (uint32_t)ret, (uint32_t)userId);
    if (ret == 0 && c4fIsRealUser(userId)) return userId;

    c4fLog("no real user found; using fallback 0x%08x\n", C4F_VDA_USER_FALLBACK);
    return C4F_VDA_USER_FALLBACK;
}

/* Which user the device is created for. SplashDown maps any real account id
 * (like 1A2B3C4D) onto 0x10000000. On our console a device created that way
 * acts as the signed-in user: PS works, but there is no "Who's using this
 * controller?" screen (2026-10-04). C4F_VDA_USER overrides it at build time to
 * look for a value that makes the device arrive unassigned; Ghostcontrol uses
 * 1 as an "anonymous" slot and sees -1 on devices waiting for assignment. */
static int32_t c4fVdaUserId(int32_t userId)
{
#ifdef C4F_VDA_USER
    (void)userId;
    return (int32_t)(C4F_VDA_USER);
#else
    if (userId >= 0x10000000 && userId <= 0x1000000f) return userId;
    return C4F_VDA_USER_FALLBACK;
#endif
}

/* ---- /dev/klog scanning ----
 *
 * AddDevice hands back a status, not a handle. The handle we need is the MBus
 * DeviceId, and the only way for a payload to learn it is to read the kernel log
 * while the device is being added. Ugly, but it is what works.
 */

/* Prefer GoldHEN's stream. Both connection and capture checks are bounded:
 * a successful TCP connection alone does not mean this client gets any logs. */
static int c4fKlogConnectLocal(void)
{
    struct sockaddr_in addr;
    int s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) {
        c4fLog("klog socket() failed errno=%d\n", errno);
        return -1;
    }

    (void)memset(&addr, 0, sizeof(addr));
#ifdef __FreeBSD__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C4F_KLOG_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (s >= (int)FD_SETSIZE || fcntl(s, F_SETFL, O_NONBLOCK) < 0) goto fail;
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) goto fail;
        uint64_t deadline = c4fVdaNowMs() + 700;
        for (;;) {
            if (g_waitCallback) g_waitCallback(g_waitContext);
            fd_set wr; FD_ZERO(&wr); FD_SET(s, &wr);
            struct timeval timeout = {0, 20000};
            int ready = select(s + 1, NULL, &wr, NULL, &timeout);
            if (ready > 0) {
                int error = 0; socklen_t size = sizeof(error);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &size)) goto fail;
                if (error) { errno = error; goto fail; }
                break;
            }
            if (ready < 0 && errno != EINTR) goto fail;
            if (c4fVdaNowMs() >= deadline) { errno = ETIMEDOUT; goto fail; }
        }
    }
    return s;
fail:
    c4fLog("klog socket unavailable errno=%d\n", errno);
    close(s);
    return -1;
}

/* Drained and proven by a marker that comes back, or closed. */
static int c4fKlogVerified(int fd, const char *what)
{
    c4fKlogDrain(fd);
    if (c4fKlogSelfTest(fd) == 0) {
        c4fLog("reading klog through %s\n", what);
        return fd;
    }
    close(fd);
    return -1;
}

/* GoldHEN's klog server opens /dev/klog only while it has a client and serves
 * one client at a time. After a client leaves it can go minutes without
 * serving the next one: on the console (2026-10-04) every reconnect after a
 * release read 0 bytes for 4-5 minutes. So the browser service never uses the
 * stream; it reads the device itself, only while a controller signs in. */
int c4fKlogOpenDevice(void)
{
    int fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        c4fLog("open(/dev/klog) failed errno=%d (16: GoldHEN's klog server has a client)\n", errno);
        return -1;
    }
    return c4fKlogVerified(fd, "/dev/klog");
}

int c4fKlogOpen(void)
{
    int fd = c4fKlogOpenDevice();
    if (fd >= 0) return fd;
    /* The diagnostic stages run once, usually with the PC's klog viewer
     * attached, which holds the device: then the stream is the only way. */
    fd = c4fKlogConnectLocal();
    if (fd >= 0 && (fd = c4fKlogVerified(fd, "127.0.0.1:3232")) >= 0) return fd;
    c4fLog("no verified klog source\n");
    return -1;
}

/* Throws away whatever is already buffered so only new lines get scanned. A
 * fresh connection to the klog server may replay a backlog, and an old "device
 * added" line in it would hand us a stale DeviceId. Keep reading until the
 * stream has been quiet for a while rather than stopping at the first empty
 * read, because the backlog does not arrive all at once. */
void c4fKlogDrain(int fd)
{
    char tmp[1024];
    int quietMs = 0;
    int totalMs = 0;
    long bytes = 0;
    uint64_t started = c4fVdaNowMs();

    if (fd < 0) return;
    while (quietMs < 300 && totalMs < 3000 && c4fVdaNowMs() - started < 3000) {
        if (g_waitCallback) g_waitCallback(g_waitContext);
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            bytes += n;
            quietMs = 0;
            continue;
        }
        if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) break;
        usleep(20000);
        quietMs += 20;
        totalMs += 20;
    }
    c4fLog("klog drain: discarded %ld bytes of backlog\n", bytes);
}

/* Writes a marker into the kernel log and checks we can read it back. If this
 * fails, stage 3 cannot learn the virtual device's handle. */
int c4fKlogSelfTest(int fd)
{
    static unsigned sequence;
    char marker[96];
    char buf[512];
    char window[sizeof(buf) + sizeof(marker)];
    size_t keep = 0;
    long bytes = 0;
    int found = 0;
    uint64_t started = c4fVdaNowMs();
    int savedKlog;

    if (fd < 0) {
        c4fLog("klog self-test skipped: no klog source\n");
        return -1;
    }

    /* Straight to klog, not through c4fLog, so the mirror file only gets the
     * result line. */
    snprintf(marker, sizeof(marker), "c4f-klog-selftest-%d-%llu-%u", getpid(),
             (unsigned long long)started, ++sequence);
    klog_printf("[c4f] %s\n", marker);

    savedKlog = c4fLogKlogEnabled();
    c4fLogSetKlog(0);
    while (c4fVdaNowMs() - started < 2000 && !found) {
        if (g_waitCallback) g_waitCallback(g_waitContext);
        ssize_t n = read(fd, window + keep, sizeof(buf));
        if (n <= 0) {
            if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) break;
            usleep(20000);
            continue;
        }
        bytes += n;
        {
            size_t total = keep + (size_t)n;
            size_t tail = strlen(marker) - 1;

            window[total] = '\0';
            if (strstr(window, marker)) found = 1;
            /* Carry the tail over so a marker split across two reads still matches. */
            if (total > tail) {
                (void)memmove(window, window + total - tail, tail);
                keep = tail;
            } else {
                keep = total;
            }
        }
    }
    c4fLogSetKlog(savedKlog);

    c4fLog("klog self-test: %s (%ld bytes read in %d ms)\n",
           found ? "PASS, our own line came back" : "FAIL, marker never seen",
           bytes, (int)(c4fVdaNowMs() - started));
    return found ? 0 : -1;
}

static uint64_t c4fParseHexAfter(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    uint64_t value = 0;

    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!isxdigit((unsigned char)*p)) return 0;

    while (isxdigit((unsigned char)*p)) {
        char c = *p++;
        value <<= 4;
        if (c >= '0' && c <= '9') value |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (uint64_t)(10 + c - 'a');
        else value |= (uint64_t)(10 + c - 'A');
    }
    return value;
}

static uint64_t c4fParseDeviceId(const char *line)
{
    static const char *keys[] = { "DeviceId", "DeviceID", "deviceId", "deviceID" };
    size_t i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        uint64_t id = c4fParseHexAfter(line, keys[i]);
        if (id) return id;
    }
    return 0;
}

static int c4fIsAddLine(const char *line)
{
    return strstr(line, "DEVICE_ADDED") != NULL || strstr(line, " ADD") != NULL;
}

/* A PS4 virtual pad is logged as the Remote Play device: type 4, subType 2. */
static int c4fIsVirtualAddLine(const char *line)
{
    int looksVirtual = strstr(line, "REMOTEPLAY") != NULL ||
                       strstr(line, "type:4") != NULL || strstr(line, "type=4") != NULL ||
                       strstr(line, "subType:2") != NULL || strstr(line, "subType=2") != NULL;
    return c4fIsAddLine(line) && looksVirtual;
}

int c4fKlogFindDeviceId(int fd, uint64_t *outDeviceId, int timeoutMs)
{
    char buf[512];
    char line[1024];
    size_t lineLen = 0;
    uint64_t weakId = 0;
    int loops = timeoutMs / 20;
    int result = -1;
    int savedKlog;
    int i;

    if (fd < 0 || !outDeviceId) return -1;
    if (loops < 1) loops = 1;

    /* Our own writes would be read straight back and bury the kernel lines. */
    savedKlog = c4fLogKlogEnabled();
    c4fLogSetKlog(0);
    c4fLog("scanning /dev/klog for a virtual DeviceId, up to %d ms\n", timeoutMs);

    for (i = 0; i < loops && result != 0; i++) {
        if (g_waitCallback) g_waitCallback(g_waitContext);
        ssize_t n = read(fd, buf, sizeof(buf));
        ssize_t k;

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(20000);
                continue;
            }
            c4fLog("klog read errno=%d\n", errno);
            break;
        }
        if (n == 0) {
            usleep(20000);
            continue;
        }

        for (k = 0; k < n; k++) {
            char c = buf[k];
            uint64_t id;

            if (c == '\r') continue;
            if (c != '\n' && lineLen + 1 < sizeof(line)) {
                line[lineLen++] = c;
                continue;
            }

            line[lineLen] = '\0';
            lineLen = 0;

            id = c4fParseDeviceId(line);
            if (id && c4fIsVirtualAddLine(line)) {
                *outDeviceId = id;
                c4fLog("matched virtual device line: %s\n", line);
                result = 0;
                break;
            }
            /* Any other "device added" line is a weaker candidate: keep the
             * first one in case the exact match never arrives. */
            if (id && c4fIsAddLine(line) && !weakId) {
                weakId = id;
                c4fLog("weak candidate DeviceId=0x%llx from: %s\n",
                       (unsigned long long)weakId, line);
            }
        }
    }

    if (result != 0 && weakId) {
        *outDeviceId = weakId;
        c4fLog("no exact match; taking weak candidate DeviceId=0x%llx\n",
               (unsigned long long)weakId);
        result = 0;
    }
    if (result != 0) c4fLog("no DeviceId seen in klog\n");

    c4fLogSetKlog(savedKlog);
    return result;
}

/* ---- device lifecycle ---- */

int c4fVirtualPadAdd(C4fVirtualPad *out, int32_t userId, int klogFd)
{
    return c4fVirtualPadAddAs(out, userId, c4fVdaUserId(userId), klogFd);
}

int c4fVirtualPadAddAs(C4fVirtualPad *out, int32_t userId, int32_t vdaUser, int klogFd)
{
    C4fVdaParam param;
    uint64_t deviceId = 0;
    int32_t ret;
    int i;

    (void)memset(out, 0, sizeof(*out));
    out->handle = -1;
    out->userId = C4F_USER_ID_INVALID;

    (void)memset(&param, 0, sizeof(param));
    param.size = (int32_t)sizeof(param);
    param.userId = vdaUser;
    /* A marker, so the log shows whether the call writes anything back. */
    for (i = 0; i < 6; i++) param.pad[i] = (int32_t)0xdeadbeef;

    c4fLog("AddDevice: size=%d userId=0x%08x type=%d\n",
           param.size, (uint32_t)param.userId, C4F_VIRTUAL_DEVICE_TYPE);
    ret = scePadVirtualDeviceAddDevice(&param, C4F_VIRTUAL_DEVICE_TYPE);
    c4fLog("AddDevice = 0x%08x pad=[0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x]\n",
           (uint32_t)ret,
           (uint32_t)param.pad[0], (uint32_t)param.pad[1], (uint32_t)param.pad[2],
           (uint32_t)param.pad[3], (uint32_t)param.pad[4], (uint32_t)param.pad[5]);

    /* A non-zero return does not have to mean failure: on PS5 SplashDown sees
     * 0x803b0006 and the device is created anyway. So look for it regardless. */
    if (klogFd >= 0 && c4fKlogFindDeviceId(klogFd, &deviceId, 2500) == 0) {
        out->deviceId = deviceId;
        out->handle = (int32_t)(deviceId & 0xffffffffu);
        out->owned = 1;
        c4fLog("DeviceId=0x%llx handle=0x%08x\n",
               (unsigned long long)deviceId, (uint32_t)out->handle);
    }

    if (out->handle < 0) {
        for (i = 0; i < 6; i++) {
            if (param.pad[i] > 0 && param.pad[i] != (int32_t)0xdeadbeef) {
                out->handle = param.pad[i];
                out->deviceId = (uint32_t)out->handle;
                out->owned = 1;
                c4fLog("handle from written-back pad[%d]=0x%08x\n",
                       i, (uint32_t)out->handle);
                break;
            }
        }
    }

    if (out->handle < 0) {
        c4fLog("ERROR: no virtual device handle\n");
        return -1;
    }
    out->userId = userId;
    out->vdaUserId = vdaUser;
    return 0;
}

int c4fVirtualPadBind(C4fVirtualPad *pad, int32_t userId)
{
    int32_t ret;

    if (!g_mbusBind) {
        c4fLog("bind skipped: MBus not resolved\n");
        return -1;
    }
    if (!pad->deviceId) {
        c4fLog("bind skipped: no DeviceId\n");
        return -1;
    }

    ret = g_mbusBind(pad->deviceId, userId);
    c4fLog("sceMbusBindDeviceWithUserId(0x%llx, 0x%08x) = 0x%08x\n",
           (unsigned long long)pad->deviceId, (uint32_t)userId, (uint32_t)ret);
    if (ret != 0) return -1;

    pad->bound = 1;
    pad->userId = userId;
    return 0;
}

/* Ghostcontrol's PS4 path makes this call with SceShellCore's authid switched
 * on for just the call, then puts the old one back. We called it with
 * SplashDown's 0x3800000000010003 and the host process crashed (stage 4,
 * 2026-10-04), so the credentials are the first suspect. */
int c4fVirtualPadBindAs(C4fVirtualPad *pad, int32_t userId, uint64_t authid)
{
    pid_t pid = getpid();
    uint64_t saved = kernel_get_ucred_authid(pid);
    int result;

    if (authid) {
        c4fLog("bind: authid 0x%016llx -> 0x%016llx for the call\n",
               (unsigned long long)saved, (unsigned long long)authid);
        (void)kernel_set_ucred_authid(pid, authid);
    }
    result = c4fVirtualPadBind(pad, userId);
    if (authid) (void)kernel_set_ucred_authid(pid, saved);
    return result;
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

    g_sampleStamp += C4F_FRAME_MS * 1000u;   /* microseconds, like a real pad */
    g_sampleCount++;
    stamped.timestamp = g_sampleStamp;
    stamped.count = g_sampleCount;

    return scePadVirtualDeviceInsertData(pad->handle, &stamped);
}

int32_t c4fVirtualPadHold(const C4fVirtualPad *pad, uint32_t buttons,
                          int durationMs, const char *label)
{
    ScePadData data;
    int32_t ret = 0;
    int frames = 0;
    int elapsed;

    if (durationMs < C4F_FRAME_MS) durationMs = C4F_FRAME_MS;
    c4fPadDataNeutral(&data);
    data.buttons = buttons;

    /* A real pad reports continuously, so one lone sample is easily missed. */
    for (elapsed = 0; elapsed < durationMs; elapsed += C4F_FRAME_MS) {
        ret = c4fVirtualPadInsert(pad, &data);
        frames++;
        usleep(C4F_FRAME_MS * 1000);
    }

    c4fLog("%s: buttons=0x%08x %dms %d frames last=0x%08x\n",
           label ? label : "hold", buttons, durationMs, frames, (uint32_t)ret);
    return ret;
}

int32_t c4fProbeAssignment(int32_t realUserId)
{
    /* Device type 3 is what we created; 0 is the ordinary "any pad" type that
     * the shell uses. Both are worth asking about. */
    static const int32_t types[] = { 0, 3 };
    int32_t users[3];
    size_t u, t;
    int32_t idx;

    users[0] = realUserId;
    users[1] = C4F_VDA_USER_FALLBACK;   /* 0x10000000 */
    users[2] = 1;

    for (u = 0; u < sizeof(users) / sizeof(users[0]); u++) {
        for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            for (idx = 0; idx < 4; idx++) {
                int32_t handle = scePadGetHandle(users[u], types[t], idx);
                if (handle >= 0) {
                    c4fLog("assigned: GetHandle(user=0x%08x type=%d idx=%d) = 0x%08x\n",
                           (uint32_t)users[u], (int)types[t], (int)idx, (uint32_t)handle);
                    return users[u];
                }
            }
        }
    }
    c4fLog("not assigned yet (no GetHandle answered)\n");
    return C4F_USER_ID_INVALID;
}

/* Only ever called with the user the virtual device was created for. Asking for
 * the signed-in account instead hands back that person's real DualShock, which
 * reads as all-zero (nobody is touching it) and is not ours to open and close. */
int32_t c4fOpenReadHandle(int32_t userId)
{
    /* Type 3 is what the device was created as, so try it before the generic 0. */
    static const int32_t types[] = { C4F_VIRTUAL_DEVICE_TYPE, 0 };
    size_t t;
    int32_t idx;

    for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        for (idx = 0; idx < 4; idx++) {
            int32_t handle = scePadGetHandle(userId, types[t], idx);
            if (handle >= 0) {
                c4fLog("read handle from GetHandle(user=0x%08x type=%d idx=%d) = 0x%08x\n",
                       (uint32_t)userId, (int)types[t], (int)idx, (uint32_t)handle);
                return handle;
            }
        }
    }
    for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        int32_t handle = scePadOpen(userId, types[t], 0, NULL);
        c4fLog("scePadOpen(user=0x%08x type=%d) = 0x%08x\n",
               (uint32_t)userId, (int)types[t], (uint32_t)handle);
        if (handle >= 0) return handle;
    }
    return -1;
}

/* Candidate input bits. The documented read-side layout first, then the two
 * bits SplashDown reported for this path. */
typedef struct {
    uint32_t    bit;
    const char *name;
} C4fBitCandidate;

static const C4fBitCandidate c4fBitCandidates[] = {
    { C4F_PAD_L3,       "L3?" },
    { C4F_PAD_R3,       "R3?" },
    { C4F_PAD_OPTIONS,  "OPTIONS?" },
    { C4F_PAD_UP,       "UP?" },
    { C4F_PAD_RIGHT,    "RIGHT?" },
    { C4F_PAD_DOWN,     "DOWN?" },
    { C4F_PAD_LEFT,     "LEFT?" },
    { C4F_PAD_L2,       "L2?" },
    { C4F_PAD_R2,       "R2?" },
    { C4F_PAD_L1,       "L1?" },
    { C4F_PAD_R1,       "R1?" },
    { C4F_PAD_TRIANGLE, "TRIANGLE?" },
    { C4F_PAD_CIRCLE,   "CIRCLE?" },
    { C4F_PAD_CROSS,    "CROSS?" },
    { C4F_PAD_SQUARE,   "SQUARE?" },
    { C4F_PAD_TOUCHPAD, "TOUCHPAD?" },
    { 0x00020000u,      "SplashDown's Cross bit" },
};

void c4fProbeButtonMap(const C4fVirtualPad *pad, int32_t readHandle)
{
    ScePadData sample;
    ScePadData readback;
    size_t i;
    int frame;
    int32_t ret;

    /* What the pad reports with nothing held: anything set here is noise that
     * has to be subtracted from every result below. */
    c4fPadDataNeutral(&sample);
    for (frame = 0; frame < 5; frame++) {
        (void)c4fVirtualPadInsert(pad, &sample);
        usleep(C4F_FRAME_MS * 1000);
    }
    (void)memset(&readback, 0, sizeof(readback));
    ret = scePadReadState(readHandle, &readback);
    c4fLog("probe baseline: ReadState = 0x%08x buttons=0x%08x connected=%u\n",
           (uint32_t)ret, readback.buttons, (unsigned)readback.connected);
    if (ret < 0) {
        c4fLog("probe aborted: cannot read the pad back\n");
        return;
    }

    for (i = 0; i < sizeof(c4fBitCandidates) / sizeof(c4fBitCandidates[0]); i++) {
        uint32_t seen = 0;

        c4fPadDataNeutral(&sample);
        sample.buttons = c4fBitCandidates[i].bit;
        for (frame = 0; frame < 5; frame++) {
            (void)c4fVirtualPadInsert(pad, &sample);
            usleep(C4F_FRAME_MS * 1000);
            (void)memset(&readback, 0, sizeof(readback));
            if (scePadReadState(readHandle, &readback) >= 0) seen |= readback.buttons;
        }
        c4fLog("probe bit 0x%08x (%s) -> reported 0x%08x\n",
               c4fBitCandidates[i].bit, c4fBitCandidates[i].name, seen);

        /* Release, so presses do not run together. */
        c4fPadDataNeutral(&sample);
        for (frame = 0; frame < 5; frame++) {
            (void)c4fVirtualPadInsert(pad, &sample);
            usleep(C4F_FRAME_MS * 1000);
        }
    }
    c4fLog("probe done\n");
}

void c4fVirtualPadRemove(C4fVirtualPad *pad)
{
    int32_t ret;

    if (!pad->owned || pad->handle < 0) return;

    /* DeleteDevice alone, as SplashDown does. sceMbusDisconnectDevice is resolved
     * (g_mbusDisconnect) but calling it before the delete has not been tried on
     * hardware, so it stays out of the cleanup path until it has. */
    ret = scePadVirtualDeviceDeleteDevice(pad->handle);
    c4fLog("DeleteDevice(0x%08x) = 0x%08x\n", (uint32_t)pad->handle, (uint32_t)ret);

    pad->owned = 0;
    pad->bound = 0;
    pad->handle = -1;
    pad->deviceId = 0;
}
