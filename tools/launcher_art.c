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
#include "language.h"
#include "screen.h"
#ifdef C4F_DIAG
#include "diag.h"
/* Host preview fixture; the native application does not link it. */
void c4fDiagPreviewPage(int page, const char *text);
#endif

int sceSystemServiceParamGetInt(int id, int *value)
{
    (void)id;
    *value = 1;
    return 0;
}

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

/* Controller test states, with example readings: 1 when state is one, 0 when it
 * is not a test state, -1 for an unknown one. */
static int c4fTesterPreview(const char *state, C4fLauncherScreen *s)
{
    C4fTesterView *v = &s->tester;
    if (strncmp(state, "tester", 6)) return 0;
    v->active = 1;
    v->selected = 0;
    snprintf(v->user[0].name, sizeof(v->user[0].name), "Alex");
    snprintf(v->user[1].name, sizeof(v->user[1].name), "Sam");
    v->user[0].used = v->user[1].used = 1;
    v->user[0].state = v->user[1].state = C4F_TESTER_READY;
    v->lx = v->ly = v->rx = v->ry = 128;
    v->quat[3] = 1000;
    s->connection.available = 1;
    s->connection.pads = 1;
    for (int i = 0; i < 4; i++) s->connection.rttMs[i] = -1;
    s->connection.rttMs[0] = 8;
    if (!strcmp(state, "tester")) {
        v->buttons = 0x4000 | 0x0400 | 0x0010 | 0x0200; /* Cross, L1, Up, R2 */
        v->lx = 40; v->ly = 90; v->rx = 210; v->r2 = 180;
        v->touches = 1; v->touchX[0] = 1300; v->touchY[0] = 420;
        v->accel[1] = 100;
    } else if (!strcmp(state, "tester-motion")) {
        v->buttons = 0x0002 | 0x100000; /* L3, touchpad click */
        v->l2 = 60;
        v->touches = 2; v->touchX[0] = 420; v->touchY[0] = 300; v->touchX[1] = 1500; v->touchY[1] = 700;
        v->accel[0] = -12; v->accel[1] = 98; v->accel[2] = 21;
        v->gyro[0] = 150; v->gyro[1] = -32; v->gyro[2] = 4;
        v->quat[0] = 120; v->quat[1] = -40; v->quat[2] = 10; v->quat[3] = 991;
        v->lightSet = 1; v->light[0] = 255;
        v->rumble = 1;
        snprintf(v->note, sizeof(v->note), "Large motor: rumbling for a second.");
    } else if (!strcmp(state, "tester-menu") || !strcmp(state, "tester-remote") ||
               !strcmp(state, "tester-menu-user") || !strcmp(state, "tester-menu-unavailable") ||
               !strcmp(state, "tester-menu-error") || !strcmp(state, "tester-menu-output") ||
               !strcmp(state, "tester-menu-no-users") || !strncmp(state, "tester-menu-rtt-", 16)) {
        v->menu = 1;
        v->focus = !strcmp(state, "tester-menu") ? C4F_TESTER_RUMBLE_LARGE : C4F_TESTER_USER;
        if (!strcmp(state, "tester-menu-user")) {
            v->selected = 1; s->connection.pads = 2; s->connection.rttMs[1] = 6;
        }
        else if (!strcmp(state, "tester-menu-unavailable")) {
            v->user[0].state = C4F_TESTER_NO_CONTROLLER;
            v->focus = C4F_TESTER_RUMBLE_LARGE;
        } else if (!strcmp(state, "tester-menu-error")) {
            v->user[0].state = C4F_TESTER_FAILED;
            v->user[0].error = 0x80920001u;
            v->focus = C4F_TESTER_ORIENTATION;
        } else if (!strcmp(state, "tester-menu-output")) {
            v->lightSet = 1; v->light[1] = 255; v->rumble = 2;
            v->focus = C4F_TESTER_RUMBLE_SMALL;
            snprintf(v->note, sizeof(v->note), "Small motor: rumbling for a second.");
        } else if (!strcmp(state, "tester-menu-no-users")) {
            v->selected = -1;
            v->user[0].used = v->user[1].used = 0;
        }
    } else if (!strcmp(state, "tester-color") || !strcmp(state, "tester-color-green") ||
               !strcmp(state, "tester-color-fine") || !strcmp(state, "tester-color-black") ||
               !strcmp(state, "tester-color-white") || !strcmp(state, "tester-color-error")) {
        v->menu = v->colorMenu = v->lightSet = 1;
        v->focus = C4F_TESTER_LIGHT_COLOR;
        v->light[0] = 51; v->light[1] = 128; v->light[2] = 230;
        if (!strcmp(state, "tester-color-green")) v->colorChannel = 1;
        else if (!strcmp(state, "tester-color-fine")) v->colorFine = 1;
        else if (!strcmp(state, "tester-color-black")) memset(v->light, 0, sizeof(v->light));
        else if (!strcmp(state, "tester-color-white")) memset(v->light, 255, sizeof(v->light));
        else if (!strcmp(state, "tester-color-error")) snprintf(v->note, sizeof(v->note), "The PS4 refused that (0xfffffffb).");
    } else if (!strcmp(state, "tester-disconnected")) {
        v->user[0].state = C4F_TESTER_NO_CONTROLLER;
    } else if (!strcmp(state, "tester-error")) {
        v->selected = 1;
        v->user[1].state = C4F_TESTER_FAILED;
        v->user[1].error = 0x80920001u;
    } else if (strncmp(state, "tester-rtt-", 11)) {
        return -1;
    }
    const char *rtt = strstr(state, "rtt-");
    if (rtt) {
        rtt += 4;
        if (!strcmp(rtt, "four")) {
            s->connection.pads = 15;
            s->connection.rttMs[0] = 0; s->connection.rttMs[1] = 8;
            s->connection.rttMs[2] = 60000; s->connection.rttMs[3] = -1;
        } else if (!strcmp(rtt, "measuring")) {
            s->connection.pads = 15;
            for (int i = 0; i < 4; i++) s->connection.rttMs[i] = -1;
        } else if (!strcmp(rtt, "no-page")) s->connection.pads = 0;
        else if (!strcmp(rtt, "unavailable")) s->connection.available = 0;
        else return -1;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "svg")) return c4fPrintSvg();
    if (argc != 4 || c4fScreenInit()) return 2;
    const char *locale = getenv("C4F_PREVIEW_LANGUAGE");
    int language = locale && !strcmp(locale, "ar") ? C4F_LANGUAGE_ARABIC :
                   locale && !strcmp(locale, "pt-BR") ? C4F_LANGUAGE_PORTUGUESE : C4F_LANGUAGE_ENGLISH;
    c4fLanguageUse(language);
    /* Optional private system font fixture. Never copied into package assets. */
    const char *fontDirectory = getenv("C4F_PREVIEW_FONT_DIR");
    if (fontDirectory) {
        const char *names[2] = { "SSTArabic-Light.otf", "SSTArabic-Roman.otf" };
        unsigned char *fonts[2] = {0}; size_t sizes[2] = {0};
        for (int i = 0; i < 2; i++) {
            char path[512]; snprintf(path, sizeof(path), "%s/%s", fontDirectory, names[i]);
            FILE *file = fopen(path, "rb");
            if (!file) return 2;
            if (fseek(file, 0, SEEK_END)) return 2;
            long length = ftell(file);
            if (length <= 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET)) return 2;
            sizes[i] = (size_t)length; fonts[i] = malloc(sizes[i]);
            if (!fonts[i] || fread(fonts[i], 1, sizes[i], file) != sizes[i]) return 2;
            fclose(file);
        }
        if (c4fTextSetArabicFonts(fonts[0], sizes[0], fonts[1], sizes[1])) return 2;
    }
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
    s.language = language; s.rtl = c4fLanguageIsArabic();
    snprintf(s.address, sizeof(s.address), "http://192.168.1.20:4264");
    const char *state = argv[2], *message = "";
    s.autorun = C4F_AUTORUN_ON;
    int tester = c4fTesterPreview(state, &s);
    if (tester < 0) return 2;
    if (tester) {
        s.running = 1; s.controllers = 2;
    } else if (!strcmp(state, "running") || !strcmp(state, "confirm") || !strcmp(state, "outdated") || !strcmp(state, "mismatch") ||
        !strcmp(state, "remote-actions-running") || !strcmp(state, "remote-confirm-cancel") ||
        !strcmp(state, "remote-confirm-stop") || !strcmp(state, "remote-confirm-diag")) {
        s.running = 1; s.controllers = 2; s.confirmStop = !strcmp(state, "confirm");
        if (!strcmp(state, "outdated")) s.autorun = C4F_AUTORUN_OUTDATED;
        if (!strcmp(state, "mismatch")) snprintf(s.runningVersion, sizeof(s.runningVersion), "older build");
        message = "Ready. Open the address on your phone or PC.";
    } else if (!strcmp(state, "setup")) {
        s.autorun = C4F_AUTORUN_OFF; message = "Not running.";
    } else if (!strcmp(state, "stopped") || !strcmp(state, "remote-actions-stopped") ||
               !strcmp(state, "remote-actions-diag") || !strcmp(state, "remote-input-diag")) {
        message = "Stopped. Ready to start again.";
    } else if (!strcmp(state, "payloader-unavailable") || !strcmp(state, "payloader-unavailable-diag")) {
        message = "Auto-start is on. PayLoader did not answer (errno 61). Enable PayLoader Server in GoldHEN > Server Settings, then retry Start.";
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
    if (s.tester.active) {
        c4fDrawTester(pixels, &s);
        return c4fWrite(argv[3], pixels, (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT);
    }
#ifdef C4F_DIAG
    if (!strcmp(state, "remote-input-diag") || !strcmp(state, "payloader-unavailable-diag")) {
        snprintf(s.inputNote, sizeof(s.inputNote),
                 "DS4: disconnected (0x00000000); TV remote: ready (0x00000000), last key 0x0d / raw 0x00000000");
        char summary[768];
        snprintf(summary, sizeof(summary),
                 ">> Example input data for layout review.\n\nConsole\n"
                 "  system software: example\n\nThis app\n  version %s\n"
                 "  input: %s\n\nService (port 4264)\n  not running\n\n"
                 "Auto-start\n  state: on\n\nLast start from this app\n  message: %s\n",
                 C4F_LAUNCHER_VERSION, s.inputNote, s.message);
        c4fDiagPreviewPage(0, summary);
    }
    c4fDrawDiag(pixels, &s);
#else
    c4fDrawLauncher(pixels, &s);
#endif
    return c4fWrite(argv[3], pixels, (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT);
}
