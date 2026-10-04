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
/* DOWN is returned only for a refused local connection. Ambiguous responses
 * must not authorize starting another copy in the shared payload host. */
int c4fLauncherProbe(C4fServiceStatus *status);
int c4fLauncherStart(const char *payload, char *message, size_t size);
int c4fLauncherStop(char *message, size_t size);
#ifdef __cplusplus
}
#endif
#endif
