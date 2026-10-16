#include "tester.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "input.h"

#define C4F_TESTER_STICKS (C4F_INPUT_BUTTON_L3 | C4F_INPUT_BUTTON_R3)
/* Held keys that keep a new menu from accepting anything yet. */
#define C4F_TESTER_MENU_KEYS (C4F_TESTER_STICKS | C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN | \
                              C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT | C4F_INPUT_BUTTON_CROSS | \
                              C4F_INPUT_BUTTON_CIRCLE)

int c4fTesterHoldUpdate(C4fTesterHold *hold, uint32_t buttons, uint64_t now)
{
    if ((buttons & C4F_TESTER_STICKS) != C4F_TESTER_STICKS) {
        hold->holding = hold->fired = 0;
        return 0;
    }
    if (!hold->holding) {
        hold->holding = 1;
        hold->since = now;
    }
    if (hold->fired || now - hold->since < C4F_TESTER_HOLD_MS) return 0;
    hold->fired = 1;
    return 1;
}

static void c4fReaderState(C4fTester *t, int slot, int state, uint32_t error)
{
    C4fTesterReader *r = &t->readers[slot];
    if ((r->state != state || r->error != error) && t->ops.state) t->ops.state(t->ops.context, slot, state, error);
    r->state = state;
    r->error = error;
}

/* Undo this test's output on a reader whose handle still works. */
static void c4fReaderQuiet(C4fTester *t, int slot)
{
    C4fTesterReader *r = &t->readers[slot];
    if (r->handle >= 0 && t->rumble && t->rumbleReader == slot && t->ops.vibrate)
        t->ops.vibrate(t->ops.context, r->handle, 0, 0);
    if (r->handle >= 0 && r->lightSet && t->ops.resetLightBar)
        t->ops.resetLightBar(t->ops.context, r->handle);
    if (t->rumble && t->rumbleReader == slot) t->rumble = 0;
    r->lightSet = 0;
}

static void c4fReaderRelease(C4fTester *t, int slot)
{
    C4fTesterReader *r = &t->readers[slot];
    if (r->handle >= 0 && r->owned && t->ops.close) t->ops.close(t->ops.context, r->handle);
    if (t->rumble && t->rumbleReader == slot) t->rumble = 0;
    r->handle = -1;
    r->owned = r->lightSet = r->seen = 0;
    r->previous = r->pressed = 0;
    memset(&r->sample, 0, sizeof(r->sample));
}

static void c4fReaderEmpty(C4fTester *t, int slot)
{
    c4fReaderRelease(t, slot);
    C4fTesterReader *r = &t->readers[slot];
    r->user = -1;
    r->state = C4F_TESTER_OPENING;
    r->error = 0;
    r->name[0] = 0;
}

/* Signed-out readers stay only while they are the one on screen. */
static void c4fTesterForgetSignedOut(C4fTester *t)
{
    for (int s = 0; s < C4F_TESTER_USERS; s++)
        if (s != t->selected && t->readers[s].user >= 0 && t->readers[s].state == C4F_TESTER_SIGNED_OUT)
            c4fReaderEmpty(t, s);
}

static void c4fTesterSelectFirst(C4fTester *t)
{
    if (t->selected >= 0 && t->readers[t->selected].user >= 0) return;
    t->selected = -1;
    for (int s = 0; s < C4F_TESTER_USERS && t->selected < 0; s++)
        if (t->readers[s].user >= 0) t->selected = s;
}

static void c4fTesterUsers(C4fTester *t, uint64_t now)
{
    int32_t users[C4F_TESTER_USERS];
    t->usersAt = now + C4F_TESTER_RETRY_MS;
    if (!t->ops.loginUsers) return;
    for (int i = 0; i < C4F_TESTER_USERS; i++) users[i] = -1;
    int count = t->ops.loginUsers(t->ops.context, users);
    if (count < 0) return; /* keep what is known until the next try */
    if (count > C4F_TESTER_USERS) count = C4F_TESTER_USERS;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        C4fTesterReader *r = &t->readers[s];
        if (r->user < 0) continue;
        int present = 0;
        for (int i = 0; i < count; i++) present |= users[i] == r->user;
        if (present) {
            if (r->state == C4F_TESTER_SIGNED_OUT) {
                c4fReaderState(t, s, C4F_TESTER_OPENING, 0);
                r->retryAt = now;
            }
        } else if (r->state != C4F_TESTER_SIGNED_OUT) {
            c4fReaderQuiet(t, s);
            if (s == t->selected) {
                c4fReaderRelease(t, s);
                c4fReaderState(t, s, C4F_TESTER_SIGNED_OUT, 0);
            } else {
                c4fReaderEmpty(t, s);
            }
        }
    }
    for (int i = 0; i < count; i++) {
        int known = users[i] < 0;
        for (int s = 0; s < C4F_TESTER_USERS && !known; s++) known = t->readers[s].user == users[i];
        for (int s = 0; s < C4F_TESTER_USERS && !known; s++) {
            C4fTesterReader *r = &t->readers[s];
            if (r->user >= 0) continue;
            r->user = users[i];
            r->name[0] = 0;
            if (t->ops.userName) t->ops.userName(t->ops.context, r->user, r->name, (int)sizeof(r->name));
            r->name[sizeof(r->name) - 1] = 0;
            /* Names are drawn in plain ASCII. */
            for (char *p = r->name; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) *p = '?';
            if (!r->name[0]) snprintf(r->name, sizeof(r->name), "User %d", s + 1);
            r->retryAt = now;
            c4fReaderState(t, s, C4F_TESTER_OPENING, 0);
            known = 1;
        }
    }
    c4fTesterSelectFirst(t);
}

static void c4fReaderPoll(C4fTester *t, int slot, uint64_t now)
{
    C4fTesterReader *r = &t->readers[slot];
    r->pressed = 0;
    if (r->user < 0 || r->state == C4F_TESTER_SIGNED_OUT) return;
    if (r->handle < 0) {
        if (now < r->retryAt || !t->ops.open) return;
        int owned = 0, handle = t->ops.open(t->ops.context, r->user, &owned);
        if (handle < 0) {
            c4fReaderState(t, slot, C4F_TESTER_FAILED, (uint32_t)handle);
            r->retryAt = now + C4F_TESTER_RETRY_MS;
            return;
        }
        r->handle = handle;
        r->owned = !!owned;
        r->seen = 0;
        /* Games turn the motion sensor on for their own handles; so does this. */
        if (t->ops.motionSensor) t->ops.motionSensor(t->ops.context, handle, 1);
    }
    C4fTesterSample sample;
    memset(&sample, 0, sizeof(sample));
    int result = t->ops.read ? t->ops.read(t->ops.context, r->handle, &sample) : -1;
    if (result != 0) {
        c4fReaderRelease(t, slot);
        c4fReaderState(t, slot, C4F_TESTER_FAILED, (uint32_t)result);
        r->retryAt = now + C4F_TESTER_RETRY_MS;
        return;
    }
    if (!sample.connected) {
        memset(&r->sample, 0, sizeof(r->sample));
        r->previous = 0;
        r->seen = 0;
        c4fReaderState(t, slot, C4F_TESTER_NO_CONTROLLER, 0);
        return;
    }
    if (sample.touches > 2) sample.touches = 2;
    /* Buttons already held when reading starts are shown, but are not presses. */
    r->pressed = r->seen ? sample.buttons & ~r->previous : 0;
    r->previous = sample.buttons;
    r->seen = 1;
    r->sample = sample;
    c4fReaderState(t, slot, C4F_TESTER_READY, 0);
}

static void c4fTesterStopRumble(C4fTester *t)
{
    if (!t->rumble) return;
    C4fTesterReader *r = &t->readers[t->rumbleReader];
    if (r->handle >= 0 && t->ops.vibrate) t->ops.vibrate(t->ops.context, r->handle, 0, 0);
    t->rumble = 0;
}

static void c4fTesterNextUser(C4fTester *t, int direction)
{
    int start = t->selected >= 0 ? t->selected : 0;
    for (int step = 1; step <= C4F_TESTER_USERS; step++) {
        int s = (start + direction * step + 2 * C4F_TESTER_USERS) % C4F_TESTER_USERS;
        if (t->readers[s].user >= 0 && t->readers[s].state != C4F_TESTER_SIGNED_OUT) {
            t->selected = s;
            break;
        }
    }
    c4fTesterForgetSignedOut(t);
    t->note[0] = 0;
}

static void c4fTesterResult(C4fTester *t, int result, const char *done)
{
    if (result) snprintf(t->note, sizeof(t->note), "The PS4 refused that (0x%08x).", (unsigned)result);
    else snprintf(t->note, sizeof(t->note), "%s", done);
}

static void c4fTesterAct(C4fTester *t, int item, uint64_t now)
{
    static const uint8_t colors[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
    C4fTesterReader *r = t->selected >= 0 ? &t->readers[t->selected] : NULL;
    if (item == C4F_TESTER_RESUME) {
        t->menu = 0;
        return;
    }
    if (item == C4F_TESTER_USER) {
        c4fTesterNextUser(t, 1);
        return;
    }
    if (!r || r->user < 0 || r->handle < 0 || r->state != C4F_TESTER_READY) {
        snprintf(t->note, sizeof(t->note), "This user has no controller to test.");
        return;
    }
    switch (item) {
    case C4F_TESTER_RUMBLE_LARGE:
    case C4F_TESTER_RUMBLE_SMALL: {
        int large = item == C4F_TESTER_RUMBLE_LARGE;
        c4fTesterStopRumble(t);
        int result = t->ops.vibrate ? t->ops.vibrate(t->ops.context, r->handle, large ? 255 : 0, large ? 0 : 255) : -1;
        if (!result) {
            t->rumble = large ? 1 : 2;
            t->rumbleReader = t->selected;
            t->rumbleUntil = now + C4F_TESTER_RUMBLE_MS;
        }
        c4fTesterResult(t, result, large ? "Large motor: rumbling for a second." : "Small motor: rumbling for a second.");
        break;
    }
    case C4F_TESTER_LIGHT_RED:
    case C4F_TESTER_LIGHT_GREEN:
    case C4F_TESTER_LIGHT_BLUE: {
        const uint8_t *rgb = colors[item - C4F_TESTER_LIGHT_RED];
        int result = t->ops.lightBar ? t->ops.lightBar(t->ops.context, r->handle, rgb[0], rgb[1], rgb[2]) : -1;
        if (!result) {
            r->lightSet = 1;
            memcpy(r->light, rgb, 3);
        }
        c4fTesterResult(t, result, "Light bar set. It goes back to normal when you leave.");
        break;
    }
    case C4F_TESTER_LIGHT_RESET: {
        int result = t->ops.resetLightBar ? t->ops.resetLightBar(t->ops.context, r->handle) : -1;
        if (!result) r->lightSet = 0;
        c4fTesterResult(t, result, "Light bar reset.");
        break;
    }
    case C4F_TESTER_ORIENTATION: {
        int result = t->ops.resetOrientation ? t->ops.resetOrientation(t->ops.context, r->handle) : -1;
        c4fTesterResult(t, result, "Orientation reset.");
        break;
    }
    default:
        break;
    }
}

void c4fTesterInit(C4fTester *t, const C4fTesterOps *ops)
{
    memset(t, 0, sizeof(*t));
    if (ops) t->ops = *ops;
    t->selected = -1;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        t->readers[s].user = -1;
        t->readers[s].handle = -1;
    }
}

void c4fTesterOpen(C4fTester *t, uint64_t now)
{
    if (t->active) return;
    C4fTesterOps ops = t->ops;
    c4fTesterInit(t, &ops);
    t->active = 1;
    /* The L3+R3 hold that opened the test must end before another counts. */
    t->hold.holding = t->hold.fired = 1;
    t->hold.since = now;
    c4fTesterUsers(t, now);
}

static void c4fTesterQuietAll(C4fTester *t)
{
    c4fTesterStopRumble(t);
    for (int s = 0; s < C4F_TESTER_USERS; s++) c4fReaderQuiet(t, s);
}

void c4fTesterSuspend(C4fTester *t, uint64_t now)
{
    if (!t->active) return;
    c4fTesterQuietAll(t);
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        C4fTesterReader *r = &t->readers[s];
        c4fReaderRelease(t, s);
        if (r->user >= 0 && r->state != C4F_TESTER_SIGNED_OUT) {
            c4fReaderState(t, s, C4F_TESTER_OPENING, 0);
            r->retryAt = now;
        }
    }
    t->menu = 0;
    t->hold.holding = t->hold.fired = 1;
    t->hold.since = now;
}

void c4fTesterClose(C4fTester *t)
{
    if (!t->active) return;
    c4fTesterQuietAll(t);
    for (int s = 0; s < C4F_TESTER_USERS; s++) c4fReaderRelease(t, s);
    C4fTesterOps ops = t->ops;
    c4fTesterInit(t, &ops);
}

int c4fTesterFrame(C4fTester *t, uint64_t now, uint32_t remotePressed)
{
    if (!t->active) return 0;
    if (now >= t->usersAt) c4fTesterUsers(t, now);
    uint32_t held = 0, pressed = 0;
    int both = 0;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        c4fReaderPoll(t, s, now);
        const C4fTesterReader *r = &t->readers[s];
        if (r->state != C4F_TESTER_READY) continue;
        held |= r->sample.buttons;
        pressed |= r->pressed;
        /* One controller holding both, not two holding one each. */
        both |= (r->sample.buttons & C4F_TESTER_STICKS) == C4F_TESTER_STICKS;
    }
    if (t->rumble && now >= t->rumbleUntil) c4fTesterStopRumble(t);
    if (c4fTesterHoldUpdate(&t->hold, both ? C4F_TESTER_STICKS : 0, now) && !t->menu) {
        t->menu = 1;
        t->focus = C4F_TESTER_RESUME;
        t->armed = 0;
    }
    if (!t->menu) {
        /* The remote is not under test, so it may leave and open the menu. */
        if (remotePressed & C4F_INPUT_BUTTON_CIRCLE) {
            c4fTesterClose(t);
            return 0;
        }
        if (remotePressed & (C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_OPTIONS)) {
            t->menu = 1;
            t->focus = C4F_TESTER_RESUME;
            t->armed = 0;
        }
        return 1;
    }
    if (!t->armed) {
        /* Nothing counts until the keys that opened the menu are let go. */
        if (!(held & C4F_TESTER_MENU_KEYS)) t->armed = 1;
        return 1;
    }
    pressed |= remotePressed;
    if (pressed & C4F_INPUT_BUTTON_CIRCLE) {
        t->menu = 0;
    } else if (pressed & C4F_INPUT_BUTTON_UP) {
        t->focus = (t->focus + C4F_TESTER_ITEMS - 1) % C4F_TESTER_ITEMS;
    } else if (pressed & C4F_INPUT_BUTTON_DOWN) {
        t->focus = (t->focus + 1) % C4F_TESTER_ITEMS;
    } else if (t->focus == C4F_TESTER_USER && (pressed & (C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT))) {
        c4fTesterNextUser(t, (pressed & C4F_INPUT_BUTTON_RIGHT) ? 1 : -1);
    } else if (pressed & C4F_INPUT_BUTTON_CROSS) {
        if (t->focus == C4F_TESTER_EXIT) {
            c4fTesterClose(t);
            return 0;
        }
        c4fTesterAct(t, t->focus, now);
    }
    return 1;
}

static int16_t c4fScaled(float value, float scale)
{
    if (isnan(value)) return 0;
    float v = roundf(value * scale);
    return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
}

void c4fTesterView(const C4fTester *t, C4fTesterView *v)
{
    memset(v, 0, sizeof(*v));
    if (!t->active) return;
    v->active = 1;
    v->menu = t->menu;
    v->focus = t->focus;
    v->selected = t->selected;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        const C4fTesterReader *r = &t->readers[s];
        v->user[s].used = r->user >= 0;
        if (!v->user[s].used) continue;
        snprintf(v->user[s].name, sizeof(v->user[s].name), "%s", r->name);
        v->user[s].state = r->state;
        v->user[s].error = r->error;
    }
    snprintf(v->note, sizeof(v->note), "%s", t->note);
    if (t->selected < 0) return;
    const C4fTesterReader *r = &t->readers[t->selected];
    v->rumble = t->rumble && t->rumbleReader == t->selected ? t->rumble : 0;
    v->lightSet = r->lightSet;
    memcpy(v->light, r->light, 3);
    if (r->state != C4F_TESTER_READY) return;
    const C4fTesterSample *s = &r->sample;
    v->buttons = s->buttons;
    v->lx = s->lx; v->ly = s->ly; v->rx = s->rx; v->ry = s->ry; v->l2 = s->l2; v->r2 = s->r2;
    v->touches = s->touches;
    for (int i = 0; i < v->touches; i++) {
        v->touchX[i] = s->touch[i].x;
        v->touchY[i] = s->touch[i].y;
    }
    for (int i = 0; i < 4; i++) v->quat[i] = c4fScaled(s->quat[i], 1000);
    for (int i = 0; i < 3; i++) {
        v->accel[i] = c4fScaled(s->accel[i], 100);
        v->gyro[i] = c4fScaled(s->gyro[i], 100);
    }
}

const char *c4fTesterItemLabel(int item)
{
    switch (item) {
    case C4F_TESTER_RESUME: return "Resume";
    case C4F_TESTER_USER: return "User";
    case C4F_TESTER_RUMBLE_LARGE: return "Rumble the large motor";
    case C4F_TESTER_RUMBLE_SMALL: return "Rumble the small motor";
    case C4F_TESTER_LIGHT_RED: return "Light bar: red";
    case C4F_TESTER_LIGHT_GREEN: return "Light bar: green";
    case C4F_TESTER_LIGHT_BLUE: return "Light bar: blue";
    case C4F_TESTER_LIGHT_RESET: return "Reset the light bar";
    case C4F_TESTER_ORIENTATION: return "Reset orientation";
    case C4F_TESTER_EXIT: return "Back to the launcher";
    default: return "";
    }
}
