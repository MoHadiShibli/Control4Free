/* Control4Free -- the virtual device (VDA) lifecycle.
 *
 * One C4fVirtualPad is one virtual DualShock 4 as the system sees it. The order
 * the calls have to go in, and why, is in dev/notes/vda.md.
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
    int      bound;         /* sceMbusBindDeviceWithUserId succeeded */
} C4fVirtualPad;

/* Resolves libSceMbus. Must run before any scePad* call: libScePad imports from
 * libSceMbus and those imports are unresolved in a payload, so calling into
 * libScePad first can kill the process with PRX_NOT_RESOLVED_FUNCTION.
 * Returns 0 on success. */
int c4fMbusInit(void);

/* Starts the MBus client this process never started. which: 0 calls libScePad's
 * scePadMbusInit(), 1 calls sceMbusInit(). Both take no arguments as far as we
 * know (no published signature). Returns 0 if the function was found and
 * called; its own return value goes in *outRet. */
int c4fMbusClientInit(int which, int32_t *outRet);

/* scePadInit(), then scePadSetProcessPrivilege(1) (it fails with
 * NOT_INITIALIZED the other way round). Returns 0 if scePadInit succeeded. */
int c4fPadInit(void);

/* Picks the user to give the virtual pad: the foreground user, else the initial
 * user, else C4F_VDA_USER_FALLBACK. */
int32_t c4fPickUserId(void);

/* AddDevice, then capture the DeviceId from /dev/klog. klogFd may be -1, in which
 * case only the write-back words of the parameter block are inspected.
 * Returns 0 and fills *out on success. */
int c4fVirtualPadAdd(C4fVirtualPad *out, int32_t userId, int klogFd);

/* Same, but AddDevice gets `vdaUser` exactly as given (no mapping). */
int c4fVirtualPadAddAs(C4fVirtualPad *out, int32_t userId, int32_t vdaUser, int klogFd);

/* Keep existing pads reporting during the bounded klog waits for a new pad.
 * The callback must not read klog, create devices, or run the network loop. */
void c4fVdaSetWaitCallback(void (*callback)(void *), void *context);

/* sceMbusBindDeviceWithUserId. Until this runs the device exists but belongs to
 * nobody, so input goes nowhere. Returns 0 on success. */
int c4fVirtualPadBind(C4fVirtualPad *pad, int32_t userId);

/* The bind with `authid` switched on for just the call (0 = leave it as is).
 * Ghostcontrol uses SceShellCore's 0x4800000000000010 for this on PS4. */
int c4fVirtualPadBindAs(C4fVirtualPad *pad, int32_t userId, uint64_t authid);

#define C4F_AUTHID_SHELLCORE 0x4800000000000010ull

/* A neutral sample: sticks centred, identity quaternion, connected. */
void c4fPadDataNeutral(ScePadData *data);

/* One InsertData call. Returns the raw return value. */
int32_t c4fVirtualPadInsert(const C4fVirtualPad *pad, const ScePadData *data);

/* Holds `buttons` for durationMs, one sample every C4F_FRAME_MS. A real pad
 * reports continuously, so a single sample is often missed. */
int32_t c4fVirtualPadHold(const C4fVirtualPad *pad, uint32_t buttons,
                          int durationMs, const char *label);

/* DeleteDevice, if we own the handle. */
void c4fVirtualPadRemove(C4fVirtualPad *pad);

/* Asks the pad service whether the device has been assigned to a user yet.
 * An unassigned virtual pad answers nothing useful; once the system's user
 * selection has run, scePadGetHandle starts returning a handle for the user it
 * was given to. Logs every probe. Returns that user id, or
 * C4F_USER_ID_INVALID while it is still unassigned.
 * `realUserId` is the signed-in account to include among the candidates. */
int32_t c4fProbeAssignment(int32_t realUserId);

/* Opens a read handle on the pad for `userId`, so injected samples can be read
 * back. Returns the handle, or negative. Close it with scePadClose. */
int32_t c4fOpenReadHandle(int32_t userId);

/* Injects one candidate bit at a time and reads back what the system reports,
 * which gives the input-bit -> button map without anyone watching the screen.
 * `readHandle` must come from c4fOpenReadHandle. Logs a line per bit. */
void c4fProbeButtonMap(const C4fVirtualPad *pad, int32_t readHandle);

#define C4F_FRAME_MS 33

/* ---- klog capture ---- */
/* GoldHEN's klog server port, read from inside the console when /dev/klog is
 * already held by that server. */
#define C4F_KLOG_PORT 3232

/* /dev/klog if free, else a socket to the local klog server. -1 if neither. */
int  c4fKlogOpen(void);
void c4fKlogDrain(int fd);
/* Writes a marker to klog and reads it back. 0 = the capture path works. */
int  c4fKlogSelfTest(int fd);
/* Scans for the MBus "device added" line and returns its DeviceId. */
int  c4fKlogFindDeviceId(int fd, uint64_t *outDeviceId, int timeoutMs);

#endif /* C4F_VDA_H */
