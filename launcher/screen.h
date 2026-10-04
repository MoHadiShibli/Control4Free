#ifndef C4F_LAUNCHER_SCREEN_H
#define C4F_LAUNCHER_SCREEN_H
#include <stdint.h>
#define C4F_LAUNCHER_VERSION "0.2.0"
#define C4F_SCREEN_WIDTH 1920
#define C4F_SCREEN_HEIGHT 1080
typedef struct {
    int running, busy, controllers, confirmStop, locked;
    char address[64], message[160];
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
