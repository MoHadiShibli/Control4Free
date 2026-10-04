#ifndef C4F_LAUNCHER_SANDBOX_H
#define C4F_LAUNCHER_SANDBOX_H
/* The app sandbox refuses connections to the console itself (EACCES on both
 * 127.0.0.1 and the console's own address, seen 2026-10-04) and hides /data.
 * GoldHEN's SDK call takes the process out of it for good. */
#ifdef __cplusplus
extern "C" {
#endif

/* 0 when out of the sandbox (again: no-op); -1 with errno otherwise. */
int c4fSandboxLeave(void);
int c4fSandboxIsOut(void);

#ifdef __cplusplus
}
#endif
#endif
