#ifndef C4F_LAUNCHER_SCREEN_H
#define C4F_LAUNCHER_SCREEN_H
#include <stdint.h>
#include "tester.h"
/* Set by the Makefile from the VERSION file. */
#ifndef C4F_LAUNCHER_VERSION
#define C4F_LAUNCHER_VERSION "dev"
#endif
#define C4F_SCREEN_WIDTH 1920
#define C4F_SCREEN_HEIGHT 1080
typedef enum {
    C4F_ACTION_NONE,
    C4F_ACTION_RESUME,
    C4F_ACTION_START,
    C4F_ACTION_AUTORUN,
    C4F_ACTION_STOP,
    C4F_ACTION_CLOSE,
    C4F_ACTION_CAPTURE,
    C4F_ACTION_TESTER,
    C4F_ACTION_LANGUAGE
} C4fLauncherAction;
/* The app's language, cycled with L1 (language.h). */
enum { C4F_LANGUAGE_AUTO, C4F_LANGUAGE_ENGLISH, C4F_LANGUAGE_PORTUGUESE, C4F_LANGUAGE_COUNT };
/* Connection round trips for the selected user's virtual-controller slots.
 * A negative RTT means that no fresh measurement is available yet. */
typedef struct {
    int available;
    unsigned pads;
    int rttMs[4];
} C4fTesterConnection;
typedef struct {
    int running, busy, controllers, confirmStop, locked;
    int actionMenu, actionFocus;
    int confirmChoice, confirmMenu; /* 0 Cancel / 1 Stop; menu-origin confirmation. */
    int autorun; /* C4F_AUTORUN_* from autorun.h */
    int language; /* C4F_LANGUAGE_* */
    char address[64], message[160], autorunNote[96], runningVersion[32], inputNote[160];
    C4fTesterView tester; /* the controller test, when active */
    C4fTesterConnection connection;
#ifdef C4F_DIAG
    unsigned diagGeneration; /* bumped when a diagnostic page changes */
    int diagPage, diagScroll[8];
#endif
} C4fLauncherScreen;
#ifdef __cplusplus
extern "C" {
#endif
/* Loads the fonts and paints the backdrop once. 0 on success. */
int c4fScreenInit(void);
/* Draws a whole frame. Blending reads pixels back, so pass cached memory,
 * never the write-combined framebuffer. */
void c4fDrawLauncher(uint32_t *pixels, const C4fLauncherScreen *state);
/* Shared actions and stop-confirmation overlay for both package variants. */
void c4fDrawActionOverlay(uint32_t *pixels, const C4fLauncherScreen *state);
/* The controller test, in both package variants (tester_draw.c). */
void c4fDrawTester(uint32_t *pixels, const C4fLauncherScreen *state);
/* The package icon (size x size), in the same style. */
void c4fDrawIcon(uint32_t *pixels, int size);
#ifdef __cplusplus
}
#endif
#endif
