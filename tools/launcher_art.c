/* Renders launcher artwork on the build machine with the launcher's own drawing
 * code: the package icon, and screen states for review. Writes raw RGBA.
 *   launcher_art icon <size> <out.rgba>
 *   launcher_art screen <setup|running|stopped|outdated|confirm|busy|locked|unknown> <out.rgba> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "autorun.h"
#include "screen.h"

static int c4fWrite(const char *path, const uint32_t *pixels, size_t count)
{
    FILE *out = fopen(path, "wb");
    if (!out) return 1;
    for (size_t i = 0; i < count; i++) {
        unsigned char rgba[4] = { (unsigned char)(pixels[i] >> 16), (unsigned char)(pixels[i] >> 8),
                                  (unsigned char)pixels[i], (unsigned char)(pixels[i] >> 24) };
        fwrite(rgba, 1, 4, out);
    }
    return fclose(out) != 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 || c4fScreenInit()) return 2;
    if (!strcmp(argv[1], "icon")) {
        int size = atoi(argv[2]);
        uint32_t *pixels = size > 0 && size <= 4096 ? calloc((size_t)size * size, sizeof(uint32_t)) : NULL;
        if (!pixels) return 2;
        c4fDrawIcon(pixels, size);
        return c4fWrite(argv[3], pixels, (size_t)size * size);
    }
    if (strcmp(argv[1], "screen")) return 2;
    C4fLauncherScreen s;
    memset(&s, 0, sizeof(s));
    snprintf(s.address, sizeof(s.address), "http://192.168.1.20:4264");
    const char *state = argv[2], *message = "";
    s.autorun = C4F_AUTORUN_ON;
    if (!strcmp(state, "running") || !strcmp(state, "confirm") || !strcmp(state, "outdated")) {
        s.running = 1; s.controllers = 2; s.confirmStop = !strcmp(state, "confirm");
        if (!strcmp(state, "outdated")) s.autorun = C4F_AUTORUN_OUTDATED;
        message = "Ready. Open the address on your phone or PC.";
    } else if (!strcmp(state, "setup")) {
        s.autorun = C4F_AUTORUN_OFF; message = "Not running.";
    } else if (!strcmp(state, "stopped")) {
        message = "Stopped. Press Cross to start it again.";
    } else if (!strcmp(state, "busy")) {
        s.running = -1; s.busy = 1; s.autorun = C4F_AUTORUN_UNKNOWN; message = "Setting up Control4Free. Please wait...";
    } else if (!strcmp(state, "locked")) {
        s.running = -1; s.locked = 1;
        message = "Sent, but Control4Free never answered (127.0.0.1 no reply). Restart the PS4 before you try again.";
    } else if (!strcmp(state, "unknown")) {
        s.running = -1; s.autorun = C4F_AUTORUN_UNKNOWN;
        snprintf(s.autorunNote, sizeof(s.autorunNote), "GoldHEN did not let the app check (errno 78)");
        message = "No answer the app understands: 127.0.0.1 refused; 192.168.1.20 HTTP 404";
    } else {
        return 2;
    }
    snprintf(s.message, sizeof(s.message), "%s", message);
    uint32_t *pixels = calloc((size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT, sizeof(uint32_t));
    if (!pixels) return 2;
    c4fDrawLauncher(pixels, &s);
    return c4fWrite(argv[3], pixels, (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT);
}
