/* The controller test's logic with simulated users and pads. No console, no
 * screen: the real tester.c and navigation.c against fake native calls. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "autorun.h"
#include "input.h"
#include "navigation.h"
#include "screen.h"
#include "tester.h"

#define STICKS (C4F_INPUT_BUTTON_L3 | C4F_INPUT_BUTTON_R3)

typedef struct {
    int32_t user;
    int handle, owned, openError, readError, connected;
    int opens, closes, reads, motionOn, lightResets, orientationResets, vibrations, lightCalls, lightError;
    int vibrating, large, small, lit;
    uint8_t rgb[3];
    C4fTesterSample sample;
} FakePad;

typedef struct {
    int32_t users[4];
    int count, listError;
    FakePad pads[4];
    int stateCalls;
    const char *nameOverride;
} Fake;

static FakePad *byUser(Fake *f, int32_t user)
{
    for (int i = 0; i < 4; i++) if (f->pads[i].user == user) return &f->pads[i];
    assert(!"unknown user");
    return NULL;
}

static FakePad *byHandle(Fake *f, int handle)
{
    for (int i = 0; i < 4; i++) if (f->pads[i].handle == handle) return &f->pads[i];
    assert(!"unknown handle");
    return NULL;
}

static int fakeUsers(void *context, int32_t *users)
{
    Fake *f = context;
    if (f->listError) return f->listError;
    for (int i = 0; i < f->count; i++) users[i] = f->users[i];
    return f->count;
}

static void fakeName(void *context, int32_t user, char *name, int size)
{
    Fake *f = context;
    if (user == 1 && f->nameOverride) {
        snprintf(name, (size_t)size, "%s", f->nameOverride);
        return;
    }
    /* User 3 has no name the app can draw: the tester falls back. */
    if (user == 3) { name[0] = 0; return; }
    snprintf(name, (size_t)size, user == 1 ? "Alex" : user == 2 ? "Sam\x01" : "Kim");
}

static int fakeOpen(void *context, int32_t user, int *owned)
{
    FakePad *p = byUser(context, user);
    p->opens++;
    *owned = p->owned;
    return p->openError ? p->openError : p->handle;
}

static int fakeRead(void *context, int handle, C4fTesterSample *sample)
{
    FakePad *p = byHandle(context, handle);
    p->reads++;
    *sample = p->sample;
    sample->connected = p->connected;
    return p->readError;
}

static void fakeClose(void *context, int handle)
{
    FakePad *p = byHandle(context, handle);
    assert(p->owned);
    p->closes++;
    assert(p->closes <= p->opens);
}

static int fakeVibrate(void *context, int handle, uint8_t large, uint8_t small)
{
    FakePad *p = byHandle(context, handle);
    p->vibrations++;
    p->large = large;
    p->small = small;
    p->vibrating = large || small;
    return 0;
}

static int fakeLight(void *context, int handle, uint8_t r, uint8_t g, uint8_t b)
{
    FakePad *p = byHandle(context, handle);
    p->lightCalls++;
    if (p->lightError) return p->lightError;
    p->lit = 1;
    p->rgb[0] = r; p->rgb[1] = g; p->rgb[2] = b;
    return 0;
}

static int fakeResetLight(void *context, int handle)
{
    FakePad *p = byHandle(context, handle);
    p->lightResets++;
    p->lit = 0;
    return 0;
}

static int fakeResetOrientation(void *context, int handle)
{
    byHandle(context, handle)->orientationResets++;
    return 0;
}

static int fakeMotion(void *context, int handle, int on)
{
    byHandle(context, handle)->motionOn = on;
    return 0;
}

static void fakeState(void *context, int slot, int state, uint32_t error)
{
    (void)slot; (void)state; (void)error;
    ((Fake *)context)->stateCalls++;
}

static void setup(Fake *f, C4fTester *t)
{
    memset(f, 0, sizeof(*f));
    f->count = 2;
    f->users[0] = 1;
    f->users[1] = 2;
    for (int i = 0; i < 4; i++) {
        f->pads[i] = (FakePad){ .user = i + 1, .handle = 100 + i, .owned = 1, .connected = 1 };
        f->pads[i].sample.lx = f->pads[i].sample.ly = f->pads[i].sample.rx = f->pads[i].sample.ry = 128;
    }
    /* The launcher's own reader already holds the first user's handle. */
    f->pads[0].owned = 0;
    C4fTesterOps ops = { f, fakeUsers, fakeName, fakeOpen, fakeRead, fakeClose, fakeVibrate, fakeLight,
                         fakeResetLight, fakeResetOrientation, fakeMotion, fakeState };
    c4fTesterInit(t, &ops);
}

/* A frame with the given buttons held on the first user's controller. */
static int frame(Fake *f, C4fTester *t, uint64_t now, uint32_t held, uint32_t remote)
{
    f->pads[0].sample.buttons = held;
    return c4fTesterFrame(t, now, remote);
}

static int tap(Fake *f, C4fTester *t, uint64_t *now, uint32_t button)
{
    int active = frame(f, t, *now += 10, button, 0);
    if (active) frame(f, t, *now += 10, 0, 0);
    return active;
}

static void holdTiming(void)
{
    C4fTesterHold hold = { 0 };
    assert(!c4fTesterHoldUpdate(&hold, C4F_INPUT_BUTTON_L3, 0));
    assert(!c4fTesterHoldUpdate(&hold, STICKS, 100));
    assert(!c4fTesterHoldUpdate(&hold, STICKS | C4F_INPUT_BUTTON_CROSS, 2099));
    assert(c4fTesterHoldUpdate(&hold, STICKS, 2100));
    assert(!c4fTesterHoldUpdate(&hold, STICKS, 5000));
    assert(!c4fTesterHoldUpdate(&hold, 0, 5001));
    assert(!c4fTesterHoldUpdate(&hold, STICKS, 6000));
    assert(c4fTesterHoldUpdate(&hold, STICKS, 8000));
    puts("PASS L3+R3 must be held together for two seconds, once per hold");
}

static void readsUsers(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    c4fTesterOpen(&t, 1000);
    assert(t.active && t.selected == 0);
    f.pads[0].sample.lx = 12; f.pads[0].sample.l2 = 200; f.pads[0].sample.touches = 3;
    f.pads[0].sample.touch[1].x = 1919; f.pads[0].sample.touch[1].y = 941;
    f.pads[0].sample.accel[1] = 0.987f; f.pads[0].sample.gyro[0] = -1.234f; f.pads[0].sample.quat[3] = 0.5f;
    f.pads[0].sample.quat[0] = NAN;
    assert(frame(&f, &t, 1000, C4F_INPUT_BUTTON_CROSS, 0));
    assert(f.pads[0].opens == 1 && f.pads[1].opens == 1 && f.pads[0].motionOn && f.pads[1].motionOn);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.active && !v.menu && v.selected == 0);
    assert(v.user[0].used && v.user[1].used && !v.user[2].used);
    assert(v.user[0].id == 1 && v.user[1].id == 2 && v.user[2].id == -1 && v.user[3].id == -1);
    assert(v.user[v.selected].id == 1);
    assert(!strcmp(v.user[0].name, "Alex") && !strcmp(v.user[1].name, "Sam?"));
    assert(v.user[0].state == C4F_TESTER_READY && v.user[1].state == C4F_TESTER_READY);
    assert(v.buttons == C4F_INPUT_BUTTON_CROSS && v.lx == 12 && v.l2 == 200);
    assert(v.touches == 2 && v.touchX[1] == 1919 && v.touchY[1] == 941);
    assert(v.accel[1] == 99 && v.gyro[0] == -123 && v.quat[3] == 500 && v.quat[0] == 0);
    /* The Cross held when reading started is shown, but is not a press. */
    assert(t.readers[0].pressed == 0);
    t.selected = 1;
    c4fTesterView(&t, &v);
    assert(v.selected == 1 && v.user[v.selected].id == 2);
    t.selected = 0;
    frame(&f, &t, 1010, C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_CIRCLE, 0);
    assert(t.readers[0].pressed == C4F_INPUT_BUTTON_CIRCLE && t.active && !t.menu);

    /* A third user with no drawable name gets a stand-in. */
    f.count = 3; f.users[2] = 3;
    frame(&f, &t, 2010, 0, 0);
    c4fTesterView(&t, &v);
    assert(!strcmp(v.user[2].name, "User 3"));
    assert(v.user[2].id == 3);
    c4fTesterClose(&t);
    assert(!t.active && f.pads[0].closes == 0 && f.pads[1].closes == 1 && f.pads[2].closes == 1);
    c4fTesterView(&t, &v);
    assert(!v.active);
    for (int s = 0; s < C4F_TESTER_USERS; s++) assert(!v.user[s].used && v.user[s].id == -1);

    setup(&f, &t);
    f.count = 0;
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    c4fTesterView(&t, &v);
    assert(v.active && v.selected == -1);
    for (int s = 0; s < C4F_TESTER_USERS; s++) assert(!v.user[s].used && v.user[s].id == -1);
    c4fTesterClose(&t);
    puts("PASS readers for every signed-in user, borrowed handle kept, values in display units");
}

/* Opens the test, lets the opening hold go, then holds again until the menu opens. */
static uint64_t openMenu(Fake *f, C4fTester *t)
{
    c4fTesterOpen(t, 0);
    /* Still holding the L3+R3 that opened it: no menu, however long. */
    for (uint64_t now = 0; now <= 4000; now += 500) assert(frame(f, t, now, STICKS, 0) && !t->menu);
    frame(f, t, 4100, 0, 0);
    frame(f, t, 4200, STICKS, 0);
    frame(f, t, 6199, STICKS, 0);
    assert(!t->menu);
    frame(f, t, 6200, STICKS, 0);
    assert(t->menu && !t->armed && t->focus == C4F_TESTER_RESUME);
    return 6200;
}

static void menuActions(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    uint64_t now = openMenu(&f, &t);
    frame(&f, &t, now += 10, 0, 0);
    assert(t.armed);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_DOWN);
    assert(t.focus == C4F_TESTER_RUMBLE_LARGE);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(f.pads[0].vibrating && f.pads[0].large == 255 && f.pads[0].small == 0);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.rumble == 1 && strstr(v.note, "Large motor"));
    now = t.rumbleUntil - 1;
    frame(&f, &t, now, 0, 0);
    assert(f.pads[0].vibrating);
    frame(&f, &t, ++now, 0, 0);
    assert(!f.pads[0].vibrating && !t.rumble);

    /* Small motor, then the light bar and orientation. */
    tap(&f, &t, &now, C4F_INPUT_BUTTON_RIGHT);
    assert(t.focus == C4F_TESTER_RUMBLE_SMALL);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(f.pads[0].vibrating && f.pads[0].large == 0 && f.pads[0].small == 255);
    /* A move and a select arriving together only move. */
    frame(&f, &t, now += 10, C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.active && t.focus == C4F_TESTER_USER && !f.pads[0].lit);
    frame(&f, &t, now += 10, 0, 0);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_RIGHT);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_RIGHT);
    assert(t.focus == C4F_TESTER_LIGHT_COLOR);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(t.colorMenu && f.pads[0].lit && f.pads[0].rgb[0] == 255 && f.pads[0].rgb[1] == 255);
    c4fTesterView(&t, &v);
    assert(v.lightSet && v.light[0] == 255);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CIRCLE);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_LEFT);
    assert(t.focus == C4F_TESTER_ORIENTATION);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(f.pads[0].orientationResets == 1);

    /* Back to the launcher leaves and cleans up. */
    tap(&f, &t, &now, C4F_INPUT_BUTTON_RIGHT);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_DOWN);
    assert(t.focus == C4F_TESTER_EXIT);
    assert(!tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS));
    assert(!t.active);
    assert(f.pads[0].lightResets == 1 && !f.pads[0].lit && !f.pads[0].vibrating);
    assert(f.pads[0].closes == 0 && f.pads[1].closes == 1);
    puts("PASS menu after a fresh hold and release; rumble stops after a second; light bar reset on leaving");
}

static void switchUsers(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    uint64_t now = openMenu(&f, &t);
    frame(&f, &t, now += 10, 0, 0);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_RIGHT);
    assert(t.focus == C4F_TESTER_USER);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(t.selected == 1);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(t.selected == 0);
    /* The second user's own controller drives the menu as well. */
    f.pads[1].sample.buttons = C4F_INPUT_BUTTON_CROSS;
    frame(&f, &t, now += 10, 0, 0);
    assert(t.selected == 1);
    f.pads[1].sample.buttons = 0;
    frame(&f, &t, now += 10, 0, 0);
    /* Rumble goes to the selected user's controller. */
    tap(&f, &t, &now, C4F_INPUT_BUTTON_DOWN);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_LEFT);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(f.pads[1].vibrating && !f.pads[0].vibrating);
    /* Back closes the menu; the test stays open. */
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CIRCLE);
    assert(t.active && !t.menu);
    c4fTesterClose(&t);
    assert(!f.pads[1].vibrating && f.pads[1].closes == 1);
    puts("PASS user choice from either controller, output to the chosen user, Back resumes testing");
}

static void menuOpeningSafety(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    /* Another controller held Down before the menu opened. It must neither
     * scroll by itself nor prevent fresh input on the opening controller. */
    f.pads[1].sample.buttons = C4F_INPUT_BUTTON_DOWN;
    uint64_t now = openMenu(&f, &t);
    frame(&f, &t, now + 10, STICKS | C4F_INPUT_BUTTON_RIGHT, 0);
    assert(!t.armed && t.focus == C4F_TESTER_USER);
    frame(&f, &t, now + 20, STICKS | C4F_INPUT_BUTTON_RIGHT | C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 0 && t.menu);
    /* Releasing the opener arms activation even though Cross and another
     * controller's Down remain held. The old Cross still is not a fresh click. */
    frame(&f, &t, now + 30, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.armed && t.selected == 0);
    frame(&f, &t, now + 40, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 0);
    frame(&f, &t, now + 50, 0, 0);
    frame(&f, &t, now + 60, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 1 && t.focus == C4F_TESTER_USER);
    frame(&f, &t, now + 1000, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 1 && t.focus == C4F_TESTER_USER);
    c4fTesterClose(&t);

    setup(&f, &t);
    now = openMenu(&f, &t);
    /* No neutral frame is required between the opening gesture and a new
     * direction: release L3/R3 and press Down in the same sample. */
    frame(&f, &t, now + 10, C4F_INPUT_BUTTON_DOWN, 0);
    assert(t.armed && t.focus == C4F_TESTER_RUMBLE_LARGE);
    frame(&f, &t, now + 20, 0, 0);
    frame(&f, &t, now + 30, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].vibrating);
    c4fTesterClose(&t);

    setup(&f, &t);
    f.pads[1].sample.buttons = STICKS;
    now = openMenu(&f, &t);
    frame(&f, &t, now + 10, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.armed && t.focus == C4F_TESTER_USER); /* unrelated held sticks do not block */
    frame(&f, &t, now + 20, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 1);
    c4fTesterClose(&t);

    setup(&f, &t);
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    frame(&f, &t, 100, STICKS, 0);
    f.pads[1].sample.buttons = STICKS;
    frame(&f, &t, 1000, 0, 0);
    frame(&f, &t, 2100, 0, 0);
    assert(!t.menu); /* a different reader cannot inherit elapsed hold time */
    frame(&f, &t, 2999, 0, 0);
    assert(!t.menu);
    frame(&f, &t, 3000, 0, 0);
    assert(t.menu && t.menuOpeners == (1u << 1));
    c4fTesterClose(&t);
    puts("PASS menu release gate belongs only to its opener; fresh navigation survives; held activation never replays");
}

static void gridNavigationAndRepeat(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    uint64_t now = openMenu(&f, &t);
    assert(C4F_TESTER_MENU_COLUMNS == 4 && C4F_TESTER_MENU_ROWS == 2);
    frame(&f, &t, now + 10, 0, 0);
    frame(&f, &t, now + 20, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_USER);
    frame(&f, &t, now + 369, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_USER);
    frame(&f, &t, now + 370, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_ORIENTATION);
    frame(&f, &t, now + 469, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_ORIENTATION);
    frame(&f, &t, now + 470, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_LIGHT_COLOR);
    frame(&f, &t, now + 570, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_RESUME);
    frame(&f, &t, now + 670, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.focus == C4F_TESTER_USER); /* held Right wraps within its row */
    frame(&f, &t, now + 680, 0, 0);
    frame(&f, &t, now + 690, C4F_INPUT_BUTTON_DOWN, 0);
    assert(t.focus == C4F_TESTER_RUMBLE_SMALL);
    frame(&f, &t, now + 1040, C4F_INPUT_BUTTON_DOWN, 0);
    assert(t.focus == C4F_TESTER_USER); /* held Down wraps within its column */
    frame(&f, &t, now + 1050, C4F_INPUT_BUTTON_LEFT, 0);
    assert(t.focus == C4F_TESTER_RESUME); /* direction change moves immediately */
    frame(&f, &t, now + 1060, C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN, 0);
    assert(t.focus == C4F_TESTER_RESUME);
    frame(&f, &t, now + 1070, C4F_INPUT_BUTTON_UP, 0);
    assert(t.focus == C4F_TESTER_RESUME); /* old held half is not a fresh press */
    frame(&f, &t, now + 1080, 0, 0);
    frame(&f, &t, now + 1090, C4F_INPUT_BUTTON_UP, 0);
    assert(t.focus == C4F_TESTER_RUMBLE_LARGE); /* top wraps to the bottom of its column */
    frame(&f, &t, now + 1100, 0, 0);
    frame(&f, &t, now + 1110, C4F_INPUT_BUTTON_UP, 0);
    assert(t.focus == C4F_TESTER_RESUME);
    now += 1120;
    frame(&f, &t, now, 0, 0);
    for (int i = 0; i < 4; i++) tap(&f, &t, &now, C4F_INPUT_BUTTON_LEFT);
    assert(t.focus == C4F_TESTER_RESUME);
    tap(&f, &t, &now, C4F_INPUT_BUTTON_LEFT);
    assert(t.focus == C4F_TESTER_LIGHT_COLOR); /* first column wraps to last */

    /* Every tile has the same spatial neighbors for a controller tap and a
     * fresh HDMI-CEC direction. Row ends wrap within the row, never snake. */
    static const uint32_t directions[] = { C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_DOWN,
                                          C4F_INPUT_BUTTON_LEFT, C4F_INPUT_BUTTON_RIGHT };
    static const int neighbors[C4F_TESTER_ITEMS][4] = {
        { 4, 4, 3, 1 }, { 5, 5, 0, 2 }, { 6, 6, 1, 3 }, { 7, 7, 2, 0 },
        { 0, 0, 7, 5 }, { 1, 1, 4, 6 }, { 2, 2, 5, 7 }, { 3, 3, 6, 4 }
    };
    for (int rtl = 0; rtl < 2; rtl++) {
        t.rtl = rtl;
        for (int item = 0; item < C4F_TESTER_ITEMS; item++) {
            for (int direction = 0; direction < 4; direction++) {
                int visibleDirection = rtl && direction >= 2 ? 5 - direction : direction;
                t.focus = item;
                tap(&f, &t, &now, directions[direction]);
                assert(t.focus == neighbors[item][visibleDirection]);
                t.focus = item;
                frame(&f, &t, now += 10, 0, directions[direction]);
                assert(t.focus == neighbors[item][visibleDirection]);
                frame(&f, &t, now += 10, 0, 0);
                assert(t.focus == neighbors[item][visibleDirection]);
            }
        }
    }
    c4fTesterSuspend(&t, now + 1000);
    assert(!t.menu && !t.menuOpeners);
    for (int s = 0; s < C4F_TESTER_USERS; s++) assert(!t.readers[s].repeatDirection && !t.readers[s].repeatAt);
    c4fTesterClose(&t);
    puts("PASS 4-by-2 spatial wrap for controller/TV remote, immediate taps, 350/100 ms hold repeat and reset on focus loss");
}

static void colorPicker(int rtl)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    t.rtl = rtl;
    uint64_t now = openMenu(&f, &t);
    frame(&f, &t, now += 10, 0, 0);
    t.focus = C4F_TESTER_LIGHT_COLOR;
    frame(&f, &t, now += 10, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.colorMenu && !t.colorFine && f.pads[0].lit);
    assert(f.pads[0].rgb[0] == 255 && f.pads[0].rgb[1] == 255 && f.pads[0].rgb[2] == 255);
    frame(&f, &t, now += 500, C4F_INPUT_BUTTON_CROSS, 0);
    assert(!t.colorFine); /* The opening Cross is not also a precision toggle. */
    frame(&f, &t, now += 10, 0, 0);
    for (int i = 0; i < 18; i++) frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_LEFT);
    assert(f.pads[0].rgb[0] == 0 && f.pads[0].rgb[1] == 255 && f.pads[0].rgb[2] == 255);
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CROSS);
    assert(t.colorFine);
    /* Every exact byte value can be selected by the TV remote, without a mouse. */
    for (int value = 1; value <= 255; value++) {
        frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_RIGHT);
        assert(f.pads[0].rgb[0] == value);
    }
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_RIGHT);
    assert(f.pads[0].rgb[0] == 255); /* clamp, never wrap */
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_DOWN);
    assert(t.colorChannel == 1);
    for (int i = 0; i < 10; i++) frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_LEFT);
    assert(f.pads[0].rgb[1] == 245 && f.pads[0].rgb[0] == 255);
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_DOWN);
    assert(t.colorChannel == 2);
    for (int i = 0; i < 255; i++) frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_LEFT);
    assert(f.pads[0].rgb[2] == 0 && !f.pads[1].lit);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.colorMenu && v.colorChannel == 2 && v.colorFine && v.lightSet);
    assert(v.light[0] == 255 && v.light[1] == 245 && v.light[2] == 0);
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CIRCLE);
    assert(t.menu && !t.colorMenu && f.pads[0].lit);
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CROSS);
    assert(t.colorMenu && t.colorChannel == 2 && f.pads[0].rgb[1] == 245);
    /* Hardware refusal never fabricates a successful preview or stored value. */
    f.pads[0].lightError = -5;
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_RIGHT);
    assert(f.pads[0].rgb[2] == 0 && t.readers[0].light[2] == 0 && strstr(t.note, "refused"));
    f.pads[0].lightError = 0;
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_RIGHT);
    assert(f.pads[0].rgb[2] == 1 && !t.note[0]);
    /* Held controller input keeps its normal repeat timing in the picker. */
    frame(&f, &t, now += 10, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(f.pads[0].rgb[2] == 2);
    frame(&f, &t, now += 349, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(f.pads[0].rgb[2] == 2);
    frame(&f, &t, ++now, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(f.pads[0].rgb[2] == 3);
    frame(&f, &t, now += 10, 0, 0);
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CIRCLE);
    t.focus = C4F_TESTER_LIGHT_RESET;
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CROSS);
    assert(!t.readers[0].lightSet && !f.pads[0].lit);
    t.selected = 1;
    t.focus = C4F_TESTER_LIGHT_COLOR;
    frame(&f, &t, now += 10, 0, C4F_INPUT_BUTTON_CROSS);
    assert(t.colorMenu && f.pads[1].lit && !f.pads[0].lit);
    f.count = 1;
    frame(&f, &t, now = t.usersAt, 0, 0);
    assert(!t.colorMenu && !f.pads[1].lit && t.readers[1].state == C4F_TESTER_SIGNED_OUT);
    c4fTesterClose(&t);

    setup(&f, &t);
    now = openMenu(&f, &t);
    frame(&f, &t, now += 10, 0, 0);
    t.focus = C4F_TESTER_LIGHT_COLOR;
    f.pads[0].lightError = -5;
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    assert(!t.colorMenu && !t.readers[0].lightSet && !f.pads[0].lit);
    f.pads[0].lightError = 0;
    tap(&f, &t, &now, C4F_INPUT_BUTTON_CROSS);
    f.pads[0].connected = 0;
    frame(&f, &t, now += 10, 0, 0);
    assert(!t.colorMenu && !f.pads[0].lit && !t.readers[0].lightSet);
    c4fTesterClose(&t);
    puts("PASS RGB picker: all byte strengths, coarse/fine, controller repeat, remote control, selected user, refusal and cleanup");
}

static void menuAvailability(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    f.count = 1;
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    C4fTesterView view;
    c4fTesterView(&t, &view);
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_RESUME));
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_EXIT));
    assert(!c4fTesterItemAvailable(&view, C4F_TESTER_USER));
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_ORIENTATION));
    t.menu = t.armed = 1; t.focus = C4F_TESTER_USER;
    frame(&f, &t, 10, C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.selected == 0 && strstr(t.note, "No other user"));
    frame(&f, &t, 20, 0, 0);
    f.pads[0].connected = 0;
    frame(&f, &t, 30, 0, 0);
    c4fTesterView(&t, &view);
    assert(!c4fTesterItemAvailable(&view, C4F_TESTER_RUMBLE_LARGE));
    assert(!c4fTesterItemAvailable(&view, C4F_TESTER_LIGHT_COLOR));
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_RESUME));
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_EXIT));
    /* A signed-out user may stay selected for its status screen. The one
     * remaining signed-in user must still be available as a switch target. */
    view.user[0].state = C4F_TESTER_SIGNED_OUT;
    view.user[1].used = 1;
    view.user[1].state = C4F_TESTER_READY;
    assert(c4fTesterItemAvailable(&view, C4F_TESTER_USER));
    c4fTesterClose(&t);
    puts("PASS menu availability matches activation: user count, disconnected hardware tools, Resume and Exit");
}

static void failures(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    /* A read failure releases the handle and retries a second later. */
    f.pads[1].readError = (int)0x80920003u;
    frame(&f, &t, 10, 0, 0);
    assert(t.readers[1].state == C4F_TESTER_FAILED && t.readers[1].handle < 0 && f.pads[1].closes == 1);
    f.pads[1].readError = 0;
    frame(&f, &t, 1009, 0, 0);
    assert(f.pads[1].opens == 1);
    frame(&f, &t, 1010, 0, 0);
    assert(f.pads[1].opens == 2 && t.readers[1].state == C4F_TESTER_READY);
    /* An open failure is shown with its code and retried. */
    c4fTesterClose(&t);
    setup(&f, &t);
    f.pads[1].openError = (int)0x80920001u;
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.user[1].state == C4F_TESTER_FAILED && v.user[1].error == 0x80920001u);
    f.pads[1].openError = 0;
    frame(&f, &t, 1000, 0, 0);
    assert(t.readers[1].state == C4F_TESTER_READY);
    /* No controller: shown as such, the handle kept, nothing pressed. */
    f.pads[0].connected = 0;
    frame(&f, &t, 1010, C4F_INPUT_BUTTON_CROSS, 0);
    c4fTesterView(&t, &v);
    assert(v.user[0].state == C4F_TESTER_NO_CONTROLLER && !v.buttons && t.readers[0].handle >= 0);
    /* An action needs a controller to act on. */
    f.pads[0].connected = 1;
    frame(&f, &t, 1020, 0, 0);
    f.pads[0].connected = 0;
    t.menu = t.armed = 1;
    t.focus = C4F_TESTER_RUMBLE_LARGE;
    frame(&f, &t, 1030, 0, 0);
    f.pads[1].sample.buttons = C4F_INPUT_BUTTON_CROSS;
    frame(&f, &t, 1040, 0, 0);
    assert(!f.pads[0].vibrating && strstr(t.note, "no controller"));
    f.pads[1].sample.buttons = 0;
    c4fTesterClose(&t);
    puts("PASS read/open failures retried after a second, no-controller state, actions need a controller");
}

static void logout(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    f.count = 3; f.users[2] = 3;
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    t.selected = 1;
    /* The user on screen signs out: kept, marked, its handle closed. */
    f.count = 2; f.users[1] = 3;
    frame(&f, &t, 1000, 0, 0);
    assert(t.selected == 1 && t.readers[1].state == C4F_TESTER_SIGNED_OUT && f.pads[1].closes == 1);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.user[1].used && v.user[1].state == C4F_TESTER_SIGNED_OUT);
    assert(v.user[v.selected].id == 2);
    /* Signs in again: read again. */
    f.count = 3; f.users[1] = 2; f.users[2] = 3;
    frame(&f, &t, 2000, 0, 0);
    frame(&f, &t, 2001, 0, 0);
    assert(t.readers[1].state == C4F_TESTER_READY && f.pads[1].opens == 2);
    c4fTesterView(&t, &v);
    assert(v.user[v.selected].id == 2);
    /* A user not on screen signs out: dropped at once. */
    f.count = 2;
    frame(&f, &t, 3000, 0, 0);
    assert(t.readers[2].user < 0 && f.pads[2].closes == 1);
    c4fTesterView(&t, &v);
    assert(!v.user[2].used && v.user[2].id == -1);
    /* A failed user list keeps what is known. */
    f.listError = -1;
    frame(&f, &t, 4000, 0, 0);
    assert(t.readers[0].user == 1 && t.readers[1].user == 2);
    c4fTesterClose(&t);
    puts("PASS sign-out keeps the user on screen as signed out; others dropped; sign-in reads again");
}

static void remoteAndSuspend(void)
{
    Fake f;
    static C4fTester t;
    setup(&f, &t);
    c4fTesterOpen(&t, 0);
    frame(&f, &t, 0, 0, 0);
    /* The remote's OK opens the menu, its Back closes it, and Back again leaves. */
    frame(&f, &t, 10, 0, C4F_INPUT_BUTTON_CROSS);
    assert(t.menu);
    frame(&f, &t, 20, 0, 0);
    assert(t.armed);
    frame(&f, &t, 30, 0, C4F_INPUT_BUTTON_RIGHT);
    assert(t.focus == C4F_TESTER_USER);
    frame(&f, &t, 40, 0, C4F_INPUT_BUTTON_CIRCLE);
    assert(t.active && !t.menu);
    /* A DS4's Circle is under test and does not leave. */
    frame(&f, &t, 50, C4F_INPUT_BUTTON_CIRCLE, 0);
    assert(t.active && !t.menu);
    /* Losing focus stops output and reopens every reader. */
    t.menu = t.armed = 1;
    t.focus = C4F_TESTER_RUMBLE_LARGE;
    frame(&f, &t, 60, 0, 0);
    frame(&f, &t, 70, C4F_INPUT_BUTTON_CROSS, 0);
    t.focus = C4F_TESTER_LIGHT_COLOR;
    frame(&f, &t, 80, 0, 0);
    frame(&f, &t, 90, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].vibrating && f.pads[0].lit && f.pads[0].rgb[2] == 255);
    c4fTesterSuspend(&t, 5000);
    assert(!f.pads[0].vibrating && !f.pads[0].lit && !t.menu && t.active);
    assert(t.readers[0].handle < 0 && t.readers[1].handle < 0 && f.pads[1].closes == 1);
    frame(&f, &t, 5000, 0, 0);
    assert(t.readers[0].state == C4F_TESTER_READY && t.readers[1].state == C4F_TESTER_READY);
    assert(!frame(&f, &t, 5010, 0, C4F_INPUT_BUTTON_CIRCLE));
    assert(!t.active && f.pads[1].closes == 2);
    puts("PASS TV remote opens/closes the menu and leaves; focus loss stops output and reopens readers");
}

static void launcherEntry(void)
{
    C4fLauncherScreen screen;
    memset(&screen, 0, sizeof(screen));
    screen.autorun = C4F_AUTORUN_ON;
    assert(c4fActionAvailable(&screen, C4F_ACTION_TESTER, 0));
    assert(!strcmp(c4fActionLabel(&screen, C4F_ACTION_TESTER), "Test controllers"));
    C4fLauncherAction actions[8];
    int count = c4fActionList(&screen, 0, actions, 8), found = 0;
    for (int i = 0; i < count; i++) found |= actions[i] == C4F_ACTION_TESTER;
    assert(found && actions[count - 1] == C4F_ACTION_CLOSE);
    screen.busy = 1;
    assert(c4fActionAvailable(&screen, C4F_ACTION_TESTER, 0));
    screen.locked = 1;
    assert(!c4fActionAvailable(&screen, C4F_ACTION_TESTER, 0));
    for (int i = 0; i < C4F_TESTER_ITEMS; i++) assert(c4fTesterItemLabel(i)[0]);
    /* A DS4's Options opens it straight from the launcher, even while busy; the
     * diagnostic build keeps Options for its kernel-log capture. */
    memset(&screen, 0, sizeof(screen));
    screen.autorun = C4F_AUTORUN_ON;
    uint32_t standard, remote;
    assert(c4fNavigate(&screen, C4F_INPUT_BUTTON_OPTIONS, 0, 0, &standard, &remote) == C4F_ACTION_TESTER && !standard);
    assert(c4fNavigate(&screen, C4F_INPUT_BUTTON_OPTIONS, 0, 1, &standard, &remote) == C4F_ACTION_NONE &&
           standard == C4F_INPUT_BUTTON_OPTIONS);
    screen.busy = 1;
    assert(c4fNavigate(&screen, C4F_INPUT_BUTTON_OPTIONS, 0, 0, &standard, &remote) == C4F_ACTION_TESTER);
    screen.locked = 1;
    assert(c4fNavigate(&screen, C4F_INPUT_BUTTON_OPTIONS, 0, 0, &standard, &remote) == C4F_ACTION_NONE);
    puts("PASS Test controllers: in Actions unless locked, Options opens it, every menu item has a label");
}

static void unicodeNames(void)
{
    Fake f;
    static C4fTester t;
    const char *names[] = { "João", "أحمد DS4", "سام (Sam) 123", "أحمدمحمدأحمدمحمد" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        setup(&f, &t); f.nameOverride = names[i];
        c4fTesterOpen(&t, 0);
        C4fTesterView view; c4fTesterView(&t, &view);
        assert(!strcmp(view.user[0].name, names[i]));
        c4fTesterClose(&t);
    }
    puts("PASS Portuguese, Arabic and mixed UTF-8 usernames survive the input reader unchanged");
}

int main(void)
{
    holdTiming();
    readsUsers();
    unicodeNames();
    menuActions();
    switchUsers();
    menuOpeningSafety();
    gridNavigationAndRepeat();
    colorPicker(0);
    colorPicker(1);
    menuAvailability();
    failures();
    logout();
    remoteAndSuspend();
    launcherEntry();
    return 0;
}
