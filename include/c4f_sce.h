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
/* The user's name as the PS4 shows it: an online ID, or a local account's name. */
int32_t sceUserServiceGetUserName(int32_t userId, char *name, size_t size);
typedef struct { int32_t userId[4]; } C4fUserServiceLoginList;
typedef struct { int32_t event, userId; } C4fUserServiceEvent;
int32_t sceUserServiceGetLoginUserIdList(C4fUserServiceLoginList *list);
int32_t sceUserServiceGetEvent(C4fUserServiceEvent *event);
#define C4F_USER_EVENT_LOGOUT 1

#define C4F_USER_ID_INVALID (-1)

/* ---- libScePad ---- */
int32_t scePadInit(void);

/* Lets an unsigned process talk to the pad service at all. SplashDown calls this
 * with 1 before scePadInit; without it the VDA calls are refused. */
int32_t scePadSetProcessPrivilege(int32_t privilege);

/* The virtual-device API. AddDevice returns a status, NOT a usable handle: the
 * handle is the MBus DeviceId the kernel logs right after the call. */
int32_t scePadVirtualDeviceAddDevice(void *param, int32_t deviceType);
int32_t scePadVirtualDeviceDeleteDevice(int32_t handle);
int32_t scePadVirtualDeviceInsertData(int32_t handle, const void *padData);
int32_t scePadVirtualDeviceGetRemoteSetting(int32_t handle, void *setting) __attribute__((weak));

/* Device type 3 is what SplashDown uses on both PS4 and PS5. */
#define C4F_VIRTUAL_DEVICE_TYPE 3

/* AddDevice's parameter block. size must be sizeof the struct (32). The six
 * trailing words are unknown; we fill them with a marker so the log shows
 * whether the call writes anything back into them. */
typedef struct {
    int32_t size;
    int32_t userId;
    int32_t pad[6];
} C4fVdaParam;

/* Created for user 1, a device arrives unassigned and the PS4 opens its native
 * "Who's using this controller?" screen (confirmed on firmware 10.01). */
#define C4F_VDA_USER_SELECT 1

/* ---- libSceMbus (no SDK stub exists: loaded with dlopen) ---- */
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

/* VDA button bits use the standard read-side layout; these were confirmed on
 * firmware 10.01. The page sends the full mask (C4F_INPUT_MASK in web.c). */
#define C4F_VDI_BUTTON_PS     0x00010000u  /* moved the home screen */
#define C4F_VDI_BUTTON_CROSS  0x00004000u  /* native second-user sign-in */

#endif /* C4F_SCE_H */
