#ifndef C4F_LAUNCHER_SCREEN_H
#define C4F_LAUNCHER_SCREEN_H
#include <stdint.h>
/* Set by the Makefile from the VERSION file. */
#ifndef C4F_LAUNCHER_VERSION
#define C4F_LAUNCHER_VERSION "dev"
#endif
#define C4F_SCREEN_WIDTH 1920
#define C4F_SCREEN_HEIGHT 1080
typedef struct {
    int running, busy, controllers, confirmStop, locked;
    int autorun; /* C4F_AUTORUN_* from autorun.h */
    char address[64], message[160], autorunNote[96];
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
/* The package icon (size x size), in the same style. */
void c4fDrawIcon(uint32_t *pixels, int size);
#ifdef __cplusplus
}
#endif
#endif
