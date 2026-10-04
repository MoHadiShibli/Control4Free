/* Control4Free -- declarations for the system libraries we drive.
 *
 * The virtual-device API (VDA) is not in any public SDK header, so the pieces we
 * need are declared here. Everything was taken from ps4libdoc, the OpenOrbis
 * stubs and seregonwar/SplashDown's psbutton.c, which is the only known working
 * caller on a PS4.
 */

#ifndef C4F_SCE_H
#define C4F_SCE_H

#include <stddef.h>
#include <stdint.h>

/* ---- libSceUserService ---- */
int32_t sceUserServiceInitialize(void *params);
int32_t sceUserServiceTerminate(void);
int32_t sceUserServiceGetInitialUser(int32_t *outUserId);
int32_t sceUserServiceGetForegroundUser(int32_t *outUserId) __attribute__((weak));

#define C4F_USER_ID_INVALID (-1)

/* ---- libScePad ---- */
int32_t scePadInit(void);
int32_t scePadGetHandle(int32_t userId, int32_t type, int32_t index);
int32_t scePadOpen(int32_t userId, int32_t type, int32_t index, void *param);
int32_t scePadClose(int32_t handle);
int32_t scePadReadState(int32_t handle, void *data);

/* Lets an unsigned process talk to the pad service at all. SplashDown calls this
 * with 1 before scePadInit; without it the VDA calls are refused. */
int32_t scePadSetProcessPrivilege(int32_t privilege);

/* The virtual-device API. AddDevice returns a status, NOT a usable handle: the
 * handle is the MBus DeviceId the kernel logs right after the call. */
int32_t scePadVirtualDeviceAddDevice(void *param, int32_t deviceType);
int32_t scePadVirtualDeviceDeleteDevice(int32_t handle);
int32_t scePadVirtualDeviceInsertData(int32_t handle, const void *padData);
int32_t scePadVirtualDeviceGetRemoteSetting(int32_t handle, void *setting) __attribute__((weak));

/* Device type 3 is what SplashDown uses on both PS4 and PS5. On a PS4 it shows up
 * in klog as REMOTEPLAY, type 4, subType 2 -- the path Remote Play itself uses. */
#define C4F_VIRTUAL_DEVICE_TYPE 3

/* AddDevice's parameter block. size must be sizeof the struct (32). The six
 * trailing words are unknown; we fill them with a marker so the log shows
 * whether the call writes anything back into them. */
typedef struct {
    int32_t size;
    int32_t userId;
    int32_t pad[6];
} C4fVdaParam;

/* userId values outside the real-user range are rejected, so AddDevice gets a
 * value in 0x1000000x. 0x10000000 is the first local user on a PS4. */
#define C4F_VDA_USER_FALLBACK 0x10000000

/* ---- libSceMbus (no SDK stub exists: resolved with dlopen/dlsym) ---- */
typedef int32_t (*C4fMbusBindFn)(uint64_t deviceId, int32_t userId);
typedef int32_t (*C4fMbusDisconnectFn)(uint64_t deviceId);

#define C4F_LIBSCEMBUS_PATH "/system/common/lib/libSceMbus.sprx"

/* ---- libkernel ---- */
int sceKernelSendNotificationRequest(int unk0, void *req, size_t size, int unk1);

/* ---- Pad sample ---- */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  finger;
    uint8_t  pad[3];
} ScePadTouch;

typedef struct {
    uint8_t     fingers;
    uint8_t     pad1[3];
    uint32_t    pad2;
    ScePadTouch touch[2];
} ScePadTouchData;

/* 120 bytes, same layout the normal scePadRead path uses. */
typedef struct {
    uint32_t        buttons;
    uint8_t         lx, ly;
    uint8_t         rx, ry;
    uint8_t         l2, r2;
    uint16_t        padding;
    float           quat[4];
    float           vel[3];
    float           accel[3];
    ScePadTouchData touchData;
    uint8_t         connected;
    uint8_t         align[3];
    uint64_t        timestamp;
    uint8_t         ext[16];
    uint8_t         count;
    uint8_t         unknown[15];
} ScePadData;

/* VDA bits verified on PS4 firmware 10.01. With AddDevice user=1, the native
 * selection screen opens automatically: RIGHT=0x20 selects the next user and
 * CROSS=0x4000 signs them in. Pressing PS during selection cancels that flow.
 * The previously assumed Cross=0x20000 was not correct for this console. */
#define C4F_VDI_BUTTON_PS     0x00010000u  /* confirmed on our console: moved the home screen */
#define C4F_VDI_BUTTON_CROSS  0x00004000u  /* confirmed: native second-user sign-in */

/* Standard read-side bits. RIGHT and CROSS also verified on the VDA input
 * path; the remaining inputs still need hardware checks. */
#define C4F_PAD_L3        0x00000002u
#define C4F_PAD_R3        0x00000004u
#define C4F_PAD_OPTIONS   0x00000008u
#define C4F_PAD_UP        0x00000010u
#define C4F_PAD_RIGHT     0x00000020u
#define C4F_PAD_DOWN      0x00000040u
#define C4F_PAD_LEFT      0x00000080u
#define C4F_PAD_L2        0x00000100u
#define C4F_PAD_R2        0x00000200u
#define C4F_PAD_L1        0x00000400u
#define C4F_PAD_R1        0x00000800u
#define C4F_PAD_TRIANGLE  0x00001000u
#define C4F_PAD_CIRCLE    0x00002000u
#define C4F_PAD_CROSS     0x00004000u
#define C4F_PAD_SQUARE    0x00008000u
#define C4F_PAD_TOUCHPAD  0x00100000u

#endif /* C4F_SCE_H */
