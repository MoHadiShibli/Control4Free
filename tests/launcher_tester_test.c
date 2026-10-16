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
    int opens, closes, reads, motionOn, lightResets, orientationResets, vibrations;
    int vibrating, large, small, lit;
    uint8_t rgb[3];
    C4fTesterSample sample;
} FakePad;

typedef struct {
    int32_t users[4];
    int count, listError;
    FakePad pads[4];
    int stateCalls;
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
    (void)context;
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
    assert(!strcmp(v.user[0].name, "Alex") && !strcmp(v.user[1].name, "Sam?"));
    assert(v.user[0].state == C4F_TESTER_READY && v.user[1].state == C4F_TESTER_READY);
    assert(v.buttons == C4F_INPUT_BUTTON_CROSS && v.lx == 12 && v.l2 == 200);
    assert(v.touches == 2 && v.touchX[1] == 1919 && v.touchY[1] == 941);
    assert(v.accel[1] == 99 && v.gyro[0] == -123 && v.quat[3] == 500 && v.quat[0] == 0);
    /* The Cross held when reading started is shown, but is not a press. */
    assert(t.readers[0].pressed == 0);
    frame(&f, &t, 1010, C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_CIRCLE, 0);
    assert(t.readers[0].pressed == C4F_INPUT_BUTTON_CIRCLE && t.active && !t.menu);

    /* A third user with no drawable name gets a stand-in. */
    f.count = 3; f.users[2] = 3;
    frame(&f, &t, 2010, 0, 0);
    c4fTesterView(&t, &v);
    assert(!strcmp(v.user[2].name, "User 3"));
    c4fTesterClose(&t);
    assert(!t.active && f.pads[0].closes == 0 && f.pads[1].closes == 1 && f.pads[2].closes == 1);
    c4fTesterView(&t, &v);
    assert(!v.active);
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
    /* Presses while the opening keys are still down count for nothing. */
    frame(&f, &t, now + 10, STICKS | C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 20, STICKS | C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.menu && !t.armed && t.focus == C4F_TESTER_RESUME);
    frame(&f, &t, now + 30, 0, 0);
    assert(t.armed);
    frame(&f, &t, now + 40, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 50, 0, 0);
    frame(&f, &t, now + 60, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 70, 0, 0);
    assert(t.focus == C4F_TESTER_RUMBLE_LARGE);
    frame(&f, &t, now + 80, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].vibrating && f.pads[0].large == 255 && f.pads[0].small == 0);
    C4fTesterView v;
    c4fTesterView(&t, &v);
    assert(v.rumble == 1 && strstr(v.note, "Large motor"));
    frame(&f, &t, now + 1079, 0, 0);
    assert(f.pads[0].vibrating);
    frame(&f, &t, now + 1080, 0, 0);
    assert(!f.pads[0].vibrating && !t.rumble);

    /* Small motor, then the light bar and orientation. */
    frame(&f, &t, now + 1100, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 1105, 0, 0);
    /* A move and a select arriving together only move. */
    frame(&f, &t, now + 1110, C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0);
    assert(t.focus == C4F_TESTER_LIGHT_RED && !f.pads[0].vibrating && !f.pads[0].lit);
    frame(&f, &t, now + 1115, 0, 0);
    frame(&f, &t, now + 1120, C4F_INPUT_BUTTON_UP, 0);
    frame(&f, &t, now + 1125, 0, 0);
    assert(t.focus == C4F_TESTER_RUMBLE_SMALL);
    frame(&f, &t, now + 1130, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].vibrating && f.pads[0].small == 255 && f.pads[0].large == 0);
    frame(&f, &t, now + 1140, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 1150, 0, 0);
    frame(&f, &t, now + 1160, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].lit && f.pads[0].rgb[0] == 255 && f.pads[0].rgb[1] == 0);
    c4fTesterView(&t, &v);
    assert(v.lightSet && v.light[0] == 255);
    for (int i = 0; i < 4; i++) {
        frame(&f, &t, now + 1170 + i * 20, C4F_INPUT_BUTTON_DOWN, 0);
        frame(&f, &t, now + 1180 + i * 20, 0, 0);
    }
    assert(t.focus == C4F_TESTER_ORIENTATION);
    frame(&f, &t, now + 1300, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[0].orientationResets == 1);

    /* Back to the launcher leaves and cleans up. */
    frame(&f, &t, now + 1310, 0, 0);
    frame(&f, &t, now + 1320, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 1330, 0, 0);
    assert(t.focus == C4F_TESTER_EXIT);
    assert(!frame(&f, &t, now + 1340, C4F_INPUT_BUTTON_CROSS, 0));
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
    frame(&f, &t, now + 10, 0, 0);
    frame(&f, &t, now + 20, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 30, 0, 0);
    assert(t.focus == C4F_TESTER_USER);
    frame(&f, &t, now + 40, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.selected == 1);
    frame(&f, &t, now + 50, 0, 0);
    frame(&f, &t, now + 60, C4F_INPUT_BUTTON_RIGHT, 0);
    assert(t.selected == 0);
    frame(&f, &t, now + 70, 0, 0);
    /* The second user's own controller drives the menu as well. */
    f.pads[1].sample.buttons = C4F_INPUT_BUTTON_LEFT;
    frame(&f, &t, now + 80, 0, 0);
    assert(t.selected == 1);
    f.pads[1].sample.buttons = 0;
    frame(&f, &t, now + 90, 0, 0);
    /* Rumble goes to the selected user's controller. */
    frame(&f, &t, now + 100, C4F_INPUT_BUTTON_DOWN, 0);
    frame(&f, &t, now + 110, 0, 0);
    frame(&f, &t, now + 120, C4F_INPUT_BUTTON_CROSS, 0);
    assert(f.pads[1].vibrating && !f.pads[0].vibrating);
    /* Back closes the menu; the test stays open. */
    frame(&f, &t, now + 130, 0, 0);
    frame(&f, &t, now + 140, C4F_INPUT_BUTTON_CIRCLE, 0);
    assert(t.active && !t.menu);
    c4fTesterClose(&t);
    assert(!f.pads[1].vibrating && f.pads[1].closes == 1);
    puts("PASS user choice from either controller, output to the chosen user, Back resumes testing");
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
    /* Signs in again: read again. */
    f.count = 3; f.users[1] = 2; f.users[2] = 3;
    frame(&f, &t, 2000, 0, 0);
    frame(&f, &t, 2001, 0, 0);
    assert(t.readers[1].state == C4F_TESTER_READY && f.pads[1].opens == 2);
    /* A user not on screen signs out: dropped at once. */
    f.count = 2;
    frame(&f, &t, 3000, 0, 0);
    assert(t.readers[2].user < 0 && f.pads[2].closes == 1);
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
    frame(&f, &t, 30, 0, C4F_INPUT_BUTTON_DOWN);
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
    t.focus = C4F_TESTER_LIGHT_BLUE;
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

int main(void)
{
    holdTiming();
    readsUsers();
    menuActions();
    switchUsers();
    failures();
    logout();
    remoteAndSuspend();
    launcherEntry();
    return 0;
}
