/* Render the real tester through changing states. The Python runner compares
 * each warm result with a new process rendering the same state from scratch. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "screen.h"

static const char *c4fCases[] = {
    "idle", "sticks", "buttons", "motion", "touch", "red", "blue", "rumble", "note",
    "other-user", "renamed-user", "one-user", "disconnected", "error", "opening", "signed-out", "no-users",
    "menu", "menu-user", "menu-orientation", "menu-color", "menu-large", "menu-small",
    "menu-reset", "menu-exit", "menu-unavailable", "menu-error", "menu-output", "menu-no-users",
    "menu-other-user", "menu-renamed-user", "menu-one-user", "buttons-red", "buttons-blue", "touch-red",
    "rtt-known", "rtt-updated", "rtt-measuring", "rtt-no-page", "rtt-unavailable", "rtt-four", "rtt-four-measuring",
    "rtt-other-slot", "rtt-no-user", "rtt-unavailable-stale",
    "menu-rtt-known", "menu-rtt-updated", "menu-rtt-measuring", "menu-rtt-no-page", "menu-rtt-unavailable",
    "menu-rtt-four", "menu-rtt-four-measuring", "menu-rtt-other-slot", "menu-rtt-no-user", "menu-rtt-unavailable-stale",
    "color-red", "color-green", "color-blue", "color-fine", "color-black", "color-white", "color-error", "color-other-user", "color-rtt-known"
};
#define C4F_CASES ((int)(sizeof(c4fCases) / sizeof(c4fCases[0])))

static void c4fFixture(C4fLauncherScreen *s, int id)
{
    memset(s, 0, sizeof(*s));
    C4fTesterView *v = &s->tester;
    s->running = 1;
    v->active = 1;
    v->selected = 0;
    v->user[0].used = v->user[1].used = 1;
    v->user[0].state = v->user[1].state = C4F_TESTER_READY;
    snprintf(v->user[0].name, sizeof(v->user[0].name), "Alex");
    snprintf(v->user[1].name, sizeof(v->user[1].name), "Sam");
    v->lx = v->ly = v->rx = v->ry = 128;
    v->quat[3] = 1000;
    s->connection.available = 1;
    s->connection.pads = 1;
    for (int i = 0; i < 4; i++) s->connection.rttMs[i] = -1;
    s->connection.rttMs[0] = 8;
    const char *name = c4fCases[id];
    if (!strcmp(name, "sticks")) {
        v->lx = 12; v->ly = 230; v->rx = 211; v->ry = 60;
    } else if (!strcmp(name, "buttons") || !strcmp(name, "buttons-red") || !strcmp(name, "buttons-blue")) {
        v->buttons = 0x10fffe; /* face, direction, shoulder and stick buttons */
        v->l2 = 93; v->r2 = 207; v->lx = 35; v->rx = 224;
        if (strcmp(name, "buttons")) {
            v->lightSet = 1; v->light[!strcmp(name, "buttons-red") ? 0 : 2] = 255;
        }
    } else if (!strcmp(name, "motion")) {
        v->accel[0] = -12; v->accel[1] = 98; v->accel[2] = 21;
        v->gyro[0] = 150; v->gyro[1] = -32; v->gyro[2] = 4;
        v->quat[0] = 120; v->quat[1] = -40; v->quat[2] = 10; v->quat[3] = 991;
    } else if (!strcmp(name, "touch") || !strcmp(name, "touch-red")) {
        v->touches = 2;
        v->touchX[0] = 420; v->touchY[0] = 300;
        v->touchX[1] = 1500; v->touchY[1] = 700;
        if (!strcmp(name, "touch-red")) { v->lightSet = 1; v->light[0] = 255; }
    } else if (!strcmp(name, "red") || !strcmp(name, "blue")) {
        v->lightSet = 1;
        v->light[!strcmp(name, "red") ? 0 : 2] = 255;
    } else if (!strcmp(name, "rumble")) v->rumble = 1;
    else if (!strcmp(name, "note")) snprintf(v->note, sizeof(v->note), "Orientation reset.");
    else if (!strcmp(name, "other-user")) v->selected = 1;
    else if (!strcmp(name, "renamed-user")) snprintf(v->user[0].name, sizeof(v->user[0].name), "Alex second controller");
    else if (!strcmp(name, "one-user")) v->user[1].used = 0;
    else if (!strcmp(name, "disconnected")) v->user[0].state = C4F_TESTER_NO_CONTROLLER;
    else if (!strcmp(name, "error")) {
        v->user[0].state = C4F_TESTER_FAILED; v->user[0].error = 0x80920001u;
    } else if (!strcmp(name, "opening")) v->user[0].state = C4F_TESTER_OPENING;
    else if (!strcmp(name, "signed-out")) v->user[0].state = C4F_TESTER_SIGNED_OUT;
    else if (!strcmp(name, "no-users")) {
        v->selected = -1; v->user[0].used = v->user[1].used = 0;
    }
    if (!strncmp(name, "menu", 4)) {
        static const int focus[] = {
            C4F_TESTER_RESUME, C4F_TESTER_USER, C4F_TESTER_ORIENTATION, C4F_TESTER_LIGHT_COLOR,
            C4F_TESTER_RUMBLE_LARGE, C4F_TESTER_RUMBLE_SMALL, C4F_TESTER_LIGHT_RESET, C4F_TESTER_EXIT
        };
        v->menu = 1;
        v->focus = id >= 17 && id <= 24 ? focus[id - 17] : C4F_TESTER_RUMBLE_LARGE;
        if (!strcmp(name, "menu-unavailable")) v->user[0].state = C4F_TESTER_NO_CONTROLLER;
        else if (!strcmp(name, "menu-error")) {
            v->user[0].state = C4F_TESTER_FAILED; v->user[0].error = 0x80920001u;
        } else if (!strcmp(name, "menu-output")) {
            v->lightSet = 1; v->light[1] = 255; v->rumble = 2;
            snprintf(v->note, sizeof(v->note), "Small motor: rumbling for a second.");
        } else if (!strcmp(name, "menu-no-users")) {
            v->selected = -1; v->user[0].used = v->user[1].used = 0;
        } else if (!strcmp(name, "menu-other-user")) v->selected = 1;
        else if (!strcmp(name, "menu-renamed-user")) snprintf(v->user[0].name, sizeof(v->user[0].name), "Alex second controller");
        else if (!strcmp(name, "menu-one-user")) v->user[1].used = 0;
    }
    if (!strncmp(name, "color-", 6)) {
        v->menu = v->colorMenu = v->lightSet = 1;
        v->focus = C4F_TESTER_LIGHT_COLOR;
        v->light[0] = 51; v->light[1] = 128; v->light[2] = 230;
        if (!strcmp(name, "color-green")) v->colorChannel = 1;
        else if (!strcmp(name, "color-blue")) v->colorChannel = 2;
        else if (!strcmp(name, "color-fine")) v->colorFine = 1;
        else if (!strcmp(name, "color-black")) memset(v->light, 0, sizeof(v->light));
        else if (!strcmp(name, "color-white")) memset(v->light, 255, sizeof(v->light));
        else if (!strcmp(name, "color-error")) snprintf(v->note, sizeof(v->note), "The PS4 refused that (0xfffffffb).");
        else if (!strcmp(name, "color-other-user")) v->selected = 1;
    }
    const char *rtt = strstr(name, "rtt-");
    if (rtt) {
        rtt += 4;
        if (!strcmp(rtt, "known")) s->connection.rttMs[0] = 12;
        else if (!strcmp(rtt, "updated")) s->connection.rttMs[0] = 63;
        else if (!strcmp(rtt, "measuring")) s->connection.rttMs[0] = -1;
        else if (!strcmp(rtt, "no-page")) s->connection.pads = 0;
        else if (!strcmp(rtt, "unavailable") || !strcmp(rtt, "unavailable-stale")) {
            s->connection.available = 0;
            if (!strcmp(rtt, "unavailable-stale")) {
                s->connection.pads = 15;
                for (int i = 0; i < 4; i++) s->connection.rttMs[i] = 999;
            }
        } else if (!strcmp(rtt, "four") || !strcmp(rtt, "four-measuring")) {
            s->connection.pads = 15;
            s->connection.rttMs[0] = 0; s->connection.rttMs[1] = 8;
            s->connection.rttMs[2] = 60000; s->connection.rttMs[3] = -1;
            if (!strcmp(rtt, "four-measuring")) for (int i = 0; i < 4; i++) s->connection.rttMs[i] = -1;
        } else if (!strcmp(rtt, "other-slot")) {
            s->connection.pads = 8; s->connection.rttMs[3] = 12;
        } else if (!strcmp(rtt, "no-user")) {
            v->selected = -1; v->user[0].used = v->user[1].used = 0;
            s->connection.pads = 15;
        }
    }
}

static uint64_t c4fHash(const uint32_t *pixels)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)pixels;
    const size_t bytes = (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT * sizeof(*pixels);
    for (size_t i = 0; i < bytes; i++) { hash ^= p[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

static int c4fCaseId(const char *name)
{
    for (int i = 0; i < C4F_CASES; i++) if (!strcmp(c4fCases[i], name)) return i;
    return 0;
}

static double c4fClockMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

static int c4fCompareDouble(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void c4fMeasure(uint32_t *pixels, const char *name, int a, int b, int transition, int samples)
{
    double *times = calloc((size_t)samples, sizeof(*times));
    if (!times) exit(2);
    C4fLauncherScreen state;
    for (int i = 0; i < samples + 8; i++) {
        if (transition) {
            c4fFixture(&state, b);
            c4fDrawTester(pixels, &state);
        }
        c4fFixture(&state, transition || (i & 1) ? a : b);
        double start = c4fClockMs();
        c4fDrawTester(pixels, &state);
        double elapsed = c4fClockMs() - start;
        if (i >= 8) times[i - 8] = elapsed;
    }
    qsort(times, (size_t)samples, sizeof(*times), c4fCompareDouble);
    printf("bench %s median_ms=%.3f p95_ms=%.3f max_ms=%.3f samples=%d\n", name,
           times[samples / 2], times[(samples - 1) * 95 / 100], times[samples - 1], samples);
    free(times);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < C4F_CASES; i++) puts(c4fCases[i]);
        return 0;
    }
    if (c4fScreenInit()) return 2;
    uint32_t *pixels = calloc((size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT, sizeof(*pixels));
    if (!pixels) return 2;
    C4fLauncherScreen state;
    if (argc == 3 && (!strcmp(argv[1], "--hash") || !strcmp(argv[1], "--cold") || !strcmp(argv[1], "--entry"))) {
        int id = atoi(argv[2]);
        if (id < 0 || id >= C4F_CASES) return 2;
        if (!strcmp(argv[1], "--entry")) {
            c4fFixture(&state, 0);
            c4fDrawTester(pixels, &state);
        }
        c4fFixture(&state, id);
        double start = c4fClockMs();
        c4fDrawTester(pixels, &state);
        double elapsed = c4fClockMs() - start;
        if (strcmp(argv[1], "--hash")) printf("cold %s ms=%.3f\n", c4fCases[id], elapsed);
        else printf("%s %016llx\n", c4fCases[id], (unsigned long long)c4fHash(pixels));
    } else if (argc >= 2 && !strcmp(argv[1], "--benchmark")) {
        int samples = argc == 3 ? atoi(argv[2]) : 120;
        if (samples < 10 || samples > 10000) return 2;
        c4fMeasure(pixels, "idle", 0, 0, 0, samples);
        c4fMeasure(pixels, "sticks-buttons", 1, 2, 0, samples);
        c4fMeasure(pixels, "motion", 0, 3, 0, samples);
        c4fMeasure(pixels, "menu-still", 17, 17, 0, samples);
        c4fMeasure(pixels, "menu-focus", 18, 22, 0, samples);
        c4fMeasure(pixels, "menu-actions", 17, 29, 0, samples);
        c4fMeasure(pixels, "menu-enter", 17, 0, 1, samples);
        c4fMeasure(pixels, "menu-leave", 0, 17, 1, samples);
        c4fMeasure(pixels, "color-channel", c4fCaseId("color-red"), c4fCaseId("color-green"), 0, samples);
        c4fMeasure(pixels, "color-values", c4fCaseId("color-red"), c4fCaseId("color-black"), 0, samples);
        c4fMeasure(pixels, "color-enter", c4fCaseId("color-red"), c4fCaseId("menu-color"), 1, samples);
    } else if (argc == 1) {
        /* Both directions expose caches which retain the most recent input,
         * user, colour, menu focus, connection state or output-test feedback. */
        for (int pass = 0; pass < 3; pass++) {
            /* The same canvas is reused by the launcher's home screen when
             * leaving/reopening the tester; renderer caches must survive it. */
            C4fLauncherScreen home = { 0 };
            snprintf(home.address, sizeof(home.address), "http://192.168.1.20:4264");
            c4fDrawLauncher(pixels, &home);
            for (int step = 0; step < C4F_CASES; step++) {
                int id = pass & 1 ? C4F_CASES - step - 1 : step;
                c4fFixture(&state, id);
                c4fDrawTester(pixels, &state);
                printf("%s %016llx\n", c4fCases[id], (unsigned long long)c4fHash(pixels));
            }
        }
    } else return 2;
    free(pixels);
    return 0;
}
