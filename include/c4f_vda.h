/* Control4Free -- the virtual device (VDA) lifecycle.
 *
 * One C4fVirtualPad is one virtual DualShock 4 as the system sees it. The call
 * order and its reasons came from the spike (tag spike-final, dev notes).
 */

#ifndef C4F_VDA_H
#define C4F_VDA_H

#include <stdint.h>
#include "c4f_sce.h"

typedef struct {
    int32_t  handle;        /* what InsertData takes: low 32 bits of deviceId */
    uint64_t deviceId;      /* the MBus DeviceId, read out of klog */
    int32_t  userId;        /* the user it is bound to, or C4F_USER_ID_INVALID */
    int32_t  vdaUserId;     /* the userId AddDevice was given */
    int      owned;         /* we created it, so we delete it */
} C4fVirtualPad;

/* Loads libSceMbus. Must run before any scePad* call: libScePad imports from
 * libSceMbus and those imports are unresolved in a payload, so calling into
 * libScePad first can kill the process with PRX_NOT_RESOLVED_FUNCTION.
 * Returns 0 on success. */
int c4fMbusInit(void);

/* scePadInit(), then scePadSetProcessPrivilege(1) (it fails with
 * NOT_INITIALIZED the other way round). Returns 0 if scePadInit succeeded. */
int c4fPadInit(void);

/* AddDevice for `vdaUser`, then capture the DeviceId from klogFd (a verified
 * /dev/klog reader). Returns 0 and fills *out on success. */
int c4fVirtualPadAddAs(C4fVirtualPad *out, int32_t userId, int32_t vdaUser, int klogFd);

/* Keep existing pads reporting during the bounded klog waits for a new pad.
 * The callback must not read klog, create devices, or run the network loop. */
void c4fVdaSetWaitCallback(void (*callback)(void *), void *context);

/* A neutral sample: sticks centred, identity quaternion, connected. */
void c4fPadDataNeutral(ScePadData *data);

/* One InsertData call. Returns the raw return value. */
int32_t c4fVirtualPadInsert(const C4fVirtualPad *pad, const ScePadData *data);

/* DeleteDevice, if we own the handle. */
void c4fVirtualPadRemove(C4fVirtualPad *pad);

#define C4F_FRAME_MS 33

/* ---- klog capture ----
 * /dev/klog has a single reader. GoldHEN's klog server opens it only while it
 * has a client and serves one client at a time; after a client leaves it can
 * go minutes without serving the next one (console, 2026-10-04). So the
 * service reads the device itself, only while a controller signs in. */

/* /dev/klog, verified by a fresh marker; -1 if busy or silent. */
int  c4fKlogOpenDevice(void);
void c4fKlogDrain(int fd);
/* Writes a marker to klog and reads it back. 0 = the capture path works. */
int  c4fKlogSelfTest(int fd);
/* Scans for the MBus "device added" line and returns its DeviceId. */
int  c4fKlogFindDeviceId(int fd, uint64_t *outDeviceId, int timeoutMs);

#endif /* C4F_VDA_H */
