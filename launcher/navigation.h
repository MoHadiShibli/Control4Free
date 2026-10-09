#ifndef C4F_LAUNCHER_NAVIGATION_H
#define C4F_LAUNCHER_NAVIGATION_H
#include "screen.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Returns the number of available actions, writing up to capacity entries. */
int c4fActionList(const C4fLauncherScreen *screen, int diagnostic,
                 C4fLauncherAction *actions, int capacity);
int c4fActionAvailable(const C4fLauncherScreen *screen, C4fLauncherAction action, int diagnostic);
const char *c4fActionLabel(const C4fLauncherScreen *screen, C4fLauncherAction action);
/* Only rising edges are accepted. The remaining bits preserve diagnostic
 * paging/scrolling outside the menu, without moving both a page and focus. */
C4fLauncherAction c4fNavigate(C4fLauncherScreen *screen, uint32_t standardPressed,
                             uint32_t remotePressed, int diagnostic,
                             uint32_t *standardUnhandled, uint32_t *remoteUnhandled);
#ifdef __cplusplus
}
#endif
#endif
