#ifndef C4F_LAUNCHER_SERVICE_H
#define C4F_LAUNCHER_SERVICE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { C4F_SERVICE_UNKNOWN = -1, C4F_SERVICE_DOWN = 0, C4F_SERVICE_RUNNING = 1 };
typedef struct { int controllers, stopping; char version[24]; } C4fServiceStatus;
uint64_t c4fLauncherTimeMs(void);
/* The console's own IPv4 address, when known. The app's sandbox may not reach
 * 127.0.0.1, so it is the second way to reach Control4Free and PayLoader. */
void c4fLauncherSetHost(const char *ipv4);
/* Why the last probe could not confirm the service, for the screen. */
const char *c4fLauncherProblem(void);
/* DOWN only when nothing accepted a connection and something refused one.
 * Ambiguous answers must not allow starting another copy in the shared
 * payload host. */
int c4fLauncherProbe(C4fServiceStatus *status);
/* Whether the last probe connected anywhere, so "no answer" can be told apart
 * from "cannot reach". */
int c4fLauncherReached(void);
/* 0 running, -1 not sent, -2 sent but Control4Free did not answer. */
int c4fLauncherStart(const unsigned char *payload, size_t payloadSize, char *message, size_t size);
int c4fLauncherStop(char *message, size_t size);
#ifdef __cplusplus
}
#endif
#endif
