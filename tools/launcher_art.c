/* Renders launcher artwork on the build machine with the launcher's own drawing
 * code: the package icon, and screen states for review. Writes raw RGBA.
 *   launcher_art icon <size> <out.rgba>
 *   launcher_art screen <state> <out.rgba>
 * Includes TV-remote action and confirmation states. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "autorun.h"
#include "logo.h"
#include "screen.h"
#ifdef C4F_DIAG
#include "diag.h"
#endif

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

/* The mark as an SVG path in its design box (viewBox 0 0 1000 640), for the page. */
static int c4fPrintSvg(void)
{
    C4fPath path = { 0 };
    c4fLogoPath(&path, 1);
    for (int i = 0; i < path.count; i++) {
        const C4fPathOp *o = &path.ops[i];
        if (o->op == 'C') printf("C%g %g %g %g %g %g", o->v[0], o->v[1], o->v[2], o->v[3], o->v[4], o->v[5]);
        else printf("%c%g %g", o->op, o->v[0], o->v[1]);
        printf(i + 1 < path.count && path.ops[i + 1].op == 'M' ? "Z" : "");
    }
    printf("Z\n");
    c4fPathFree(&path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "svg")) return c4fPrintSvg();
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
    if (!strcmp(state, "running") || !strcmp(state, "confirm") || !strcmp(state, "outdated") || !strcmp(state, "mismatch") ||
        !strcmp(state, "remote-actions-running") || !strcmp(state, "remote-confirm-cancel") ||
        !strcmp(state, "remote-confirm-stop") || !strcmp(state, "remote-confirm-diag")) {
        s.running = 1; s.controllers = 2; s.confirmStop = !strcmp(state, "confirm");
        if (!strcmp(state, "outdated")) s.autorun = C4F_AUTORUN_OUTDATED;
        if (!strcmp(state, "mismatch")) snprintf(s.runningVersion, sizeof(s.runningVersion), "older build");
        message = "Ready. Open the address on your phone or PC.";
    } else if (!strcmp(state, "setup")) {
        s.autorun = C4F_AUTORUN_OFF; message = "Not running.";
    } else if (!strcmp(state, "stopped") || !strcmp(state, "remote-actions-stopped") || !strcmp(state, "remote-actions-diag")) {
        message = "Stopped. Choose Start in Actions or press Cross to start it again.";
    } else if (!strcmp(state, "busy") || !strcmp(state, "remote-actions-busy")) {
        s.running = -1; s.busy = 1; s.autorun = C4F_AUTORUN_UNKNOWN; message = "Setting up Control4Free. Please wait...";
    } else if (!strcmp(state, "locked") || !strcmp(state, "remote-actions-unavailable")) {
        s.running = -1; s.locked = 1;
        message = "Could not initialize launcher. Close with PS and retry.";
    } else if (!strcmp(state, "unknown")) {
        s.running = -1; s.autorun = C4F_AUTORUN_UNKNOWN;
        snprintf(s.autorunNote, sizeof(s.autorunNote), "GoldHEN did not let the app check (errno 78)");
        message = "No answer the app understands: 127.0.0.1 refused; 192.168.1.20 HTTP 404";
    } else {
        return 2;
    }
    if (!strncmp(state, "remote-actions-", 15)) {
        s.actionMenu = 1;
        s.actionFocus = !strcmp(state, "remote-actions-running") ? C4F_ACTION_STOP :
                        !strcmp(state, "remote-actions-stopped") ? C4F_ACTION_START :
                        !strcmp(state, "remote-actions-diag") ? C4F_ACTION_CAPTURE : C4F_ACTION_RESUME;
    } else if (!strncmp(state, "remote-confirm-", 15)) {
        s.confirmStop = s.confirmMenu = 1;
        s.confirmChoice = !strcmp(state, "remote-confirm-stop");
    }
    snprintf(s.message, sizeof(s.message), "%s", message);
    uint32_t *pixels = calloc((size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT, sizeof(uint32_t));
    if (!pixels) return 2;
#ifdef C4F_DIAG
    c4fDrawDiag(pixels, &s);
#else
    c4fDrawLauncher(pixels, &s);
#endif
    return c4fWrite(argv[3], pixels, (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT);
}
