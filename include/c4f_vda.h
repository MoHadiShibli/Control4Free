/* Control4Free -- the virtual device (VDA) lifecycle.
 *
 * One C4fVirtualPad is one virtual DualShock 4 as the system sees it. The call
 * order and its reasons came from a staged diagnostic build, in the git history
 * before 1.0.0.
 *
 * No function here blocks or waits, so the service can keep answering everyone
 * else while a controller is being created.
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

/* ---- the kernel log ----
 * /dev/klog has a single reader. GoldHEN's klog server opens it only while it
 * has a client and serves one client at a time; after a client leaves it can
 * go minutes without serving the next one (console, 2026-10-04). So the
 * service reads the device itself, only while a controller signs in, and it
 * owns the reader: these are the pieces it needs to make sense of a line. */

/* /dev/klog, non-blocking; -1 if another reader has it or it cannot be opened. */
int c4fKlogOpenDevice(void);
/* Writes one line into the kernel log, with no [c4f] prefix, so the reader can
 * prove to itself that it really receives what the kernel logs. */
void c4fKlogMark(const char *marker);
/* 1 if this is the login manager's "a virtual pad was added" event. */
int c4fKlogIsVirtualAdd(const char *line);
/* The DeviceId named in this line, or 0. */
uint64_t c4fKlogDeviceId(const char *line);

/* ---- devices ---- */

/* AddDevice for `vdaUser`. The handle is not known yet: it is the DeviceId the
 * kernel logs a moment later. Returns AddDevice's own value, which is non-zero
 * even when the device is created, so the caller waits for the log either way. */
int32_t c4fVirtualPadAdd(int32_t vdaUser);

/* Completes a pad from the DeviceId that came out of the log. */
void c4fVirtualPadAdopt(C4fVirtualPad *out, int32_t userId, int32_t vdaUser, uint64_t deviceId);

/* A neutral sample: sticks centred, identity quaternion, connected. */
void c4fPadDataNeutral(ScePadData *data);

/* One InsertData call. Returns the raw return value. */
int32_t c4fVirtualPadInsert(const C4fVirtualPad *pad, const ScePadData *data);

/* DeleteDevice, if we own the handle. */
void c4fVirtualPadRemove(C4fVirtualPad *pad);

#endif /* C4F_VDA_H */
