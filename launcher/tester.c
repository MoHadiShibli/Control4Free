#include "tester.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "input.h"

#define C4F_TESTER_STICKS (C4F_INPUT_BUTTON_L3 | C4F_INPUT_BUTTON_R3)
#define C4F_TESTER_DIRECTIONS (C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT)
#define C4F_TESTER_ACTIVATE (C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_CIRCLE)

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
    r->menuBlocked = r->repeatDirection = 0;
    r->repeatAt = 0;
    memset(&r->hold, 0, sizeof(r->hold));
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
            /* Preserve UTF-8 names. Only replace ASCII control characters. */
            for (char *p = r->name; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p == 127) *p = '?';
            r->nameGenerated = !r->name[0];
            if (r->nameGenerated) snprintf(r->name, sizeof(r->name), "User %d", s + 1);
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
        c4fReaderQuiet(t, slot);
        c4fReaderRelease(t, slot);
        c4fReaderState(t, slot, C4F_TESTER_FAILED, (uint32_t)result);
        r->retryAt = now + C4F_TESTER_RETRY_MS;
        return;
    }
    if (!sample.connected) {
        c4fReaderQuiet(t, slot);
        memset(&r->sample, 0, sizeof(r->sample));
        r->previous = 0;
        r->seen = 0;
        c4fReaderState(t, slot, C4F_TESTER_NO_CONTROLLER, 0);
        return;
    }
    if (sample.touches > 2) sample.touches = 2;
    if (!r->seen) {
        /* A hold already in progress when a reader starts cannot open a menu. */
        r->hold.holding = r->hold.fired = 1;
        r->hold.since = now;
    }
    if (!r->seen && t->menu) r->menuBlocked |= sample.buttons & (C4F_TESTER_DIRECTIONS | C4F_TESTER_ACTIVATE);
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
    C4fTesterView view;
    c4fTesterView(t, &view);
    if (!c4fTesterItemAvailable(&view, item)) {
        snprintf(t->note, sizeof(t->note), "%s", item == C4F_TESTER_USER ?
                 "No other user is signed in." : "This user has no controller to test.");
        return;
    }
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
    case C4F_TESTER_LIGHT_COLOR: {
        uint8_t rgb[3] = { 255, 255, 255 };
        if (r->lightSet) memcpy(rgb, r->light, sizeof(rgb));
        int result = t->ops.lightBar ? t->ops.lightBar(t->ops.context, r->handle, rgb[0], rgb[1], rgb[2]) : -1;
        if (!result) {
            r->lightSet = 1;
            memcpy(r->light, rgb, 3);
            t->colorMenu = 1;
            t->note[0] = 0;
        } else {
            c4fTesterResult(t, result, "");
        }
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
    t->colorMenu = 0;
    t->menuOpeners = 0;
    t->armed = 0;
}

void c4fTesterClose(C4fTester *t)
{
    if (!t->active) return;
    c4fTesterQuietAll(t);
    for (int s = 0; s < C4F_TESTER_USERS; s++) c4fReaderRelease(t, s);
    C4fTesterOps ops = t->ops;
    c4fTesterInit(t, &ops);
}

static void c4fTesterMenuOpen(C4fTester *t, int opener)
{
    t->menu = 1;
    t->colorMenu = 0;
    t->focus = C4F_TESTER_RESUME;
    t->menuOpeners = opener >= 0 ? 1u << opener : 0;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        C4fTesterReader *r = &t->readers[s];
        uint32_t buttons = r->state == C4F_TESTER_READY ? r->sample.buttons : 0;
        r->menuBlocked = buttons & (C4F_TESTER_DIRECTIONS | C4F_TESTER_ACTIVATE);
        r->repeatDirection = 0;
        r->repeatAt = 0;
    }
    t->armed = !t->menuOpeners;
}

/* A held direction repeats, but activation always needs its own fresh edge.
 * Track each controller separately so a key held on another pad cannot mask a
 * new press, or create a repeat from input held before the menu opened. */
static uint32_t c4fTesterMenuDirection(C4fTesterReader *r, uint64_t now)
{
    uint32_t held = r->sample.buttons & ~r->menuBlocked & C4F_TESTER_DIRECTIONS;
    if ((held & (C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN)) == (C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN))
        held &= ~(C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN);
    if ((held & (C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT)) == (C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT))
        held &= ~(C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT);
    uint32_t fresh = r->pressed & held;
    uint32_t candidates = fresh ? fresh : held, direction = 0;
    static const uint32_t directions[] = { C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_DOWN, C4F_INPUT_BUTTON_LEFT, C4F_INPUT_BUTTON_RIGHT };
    for (unsigned i = 0; i < sizeof(directions) / sizeof(directions[0]); i++)
        if (candidates & directions[i]) { direction = directions[i]; break; }
    if (!direction || (!fresh && direction != r->repeatDirection)) {
        r->repeatDirection = 0; r->repeatAt = 0;
        return 0;
    }
    if (fresh) {
        r->repeatDirection = direction;
        r->repeatAt = now + C4F_TESTER_REPEAT_DELAY_MS;
        return direction;
    }
    if (now < r->repeatAt) return 0;
    r->repeatAt = now + C4F_TESTER_REPEAT_MS;
    return direction;
}

static void c4fTesterMenuMove(C4fTester *t, uint32_t direction)
{
    int row = t->focus / C4F_TESTER_MENU_COLUMNS;
    int column = t->focus % C4F_TESTER_MENU_COLUMNS;
    if (direction & C4F_INPUT_BUTTON_UP) {
        row = (row + C4F_TESTER_MENU_ROWS - 1) % C4F_TESTER_MENU_ROWS;
    } else if (direction & C4F_INPUT_BUTTON_DOWN) {
        row = (row + 1) % C4F_TESTER_MENU_ROWS;
    } else if (direction & C4F_INPUT_BUTTON_LEFT) {
        column = (column + C4F_TESTER_MENU_COLUMNS - 1) % C4F_TESTER_MENU_COLUMNS;
    } else if (direction & C4F_INPUT_BUTTON_RIGHT) {
        column = (column + 1) % C4F_TESTER_MENU_COLUMNS;
    }
    t->focus = row * C4F_TESTER_MENU_COLUMNS + column;
}

static void c4fTesterColorInput(C4fTester *t, uint32_t navigation, uint32_t pressed)
{
    if (pressed & C4F_INPUT_BUTTON_CIRCLE) {
        t->colorMenu = 0;
        snprintf(t->note, sizeof(t->note), "Light bar set. It goes back to normal when you leave.");
        return;
    }
    if (navigation & C4F_INPUT_BUTTON_UP) t->colorChannel = (t->colorChannel + 2) % 3;
    else if (navigation & C4F_INPUT_BUTTON_DOWN) t->colorChannel = (t->colorChannel + 1) % 3;
    else if (navigation & (C4F_INPUT_BUTTON_LEFT | C4F_INPUT_BUTTON_RIGHT)) {
        C4fTesterReader *r = &t->readers[t->selected];
        uint8_t rgb[3];
        memcpy(rgb, r->light, sizeof(rgb));
        const int direction = navigation & C4F_INPUT_BUTTON_LEFT ? -1 : 1;
        int value = rgb[t->colorChannel] + direction * (t->colorFine ? 1 : 16);
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        if (value == rgb[t->colorChannel]) return;
        rgb[t->colorChannel] = (uint8_t)value;
        int result = t->ops.lightBar ? t->ops.lightBar(t->ops.context, r->handle, rgb[0], rgb[1], rgb[2]) : -1;
        if (!result) {
            r->lightSet = 1;
            memcpy(r->light, rgb, sizeof(rgb));
            t->note[0] = 0;
        } else c4fTesterResult(t, result, "");
    } else if (pressed & C4F_INPUT_BUTTON_CROSS) t->colorFine = !t->colorFine;
}

int c4fTesterFrame(C4fTester *t, uint64_t now, uint32_t remotePressed)
{
    if (!t->active) return 0;
    if (now >= t->usersAt) c4fTesterUsers(t, now);
    int menuOpener = -1;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        c4fReaderPoll(t, s, now);
        C4fTesterReader *r = &t->readers[s];
        if (c4fTesterHoldUpdate(&r->hold, r->state == C4F_TESTER_READY ? r->sample.buttons : 0, now) && menuOpener < 0)
            menuOpener = s;
    }
    if (t->rumble && now >= t->rumbleUntil) c4fTesterStopRumble(t);
    if (t->colorMenu && (t->selected < 0 || t->readers[t->selected].state != C4F_TESTER_READY)) {
        t->colorMenu = 0;
        snprintf(t->note, sizeof(t->note), "This user's controller is unavailable.");
    }
    if (menuOpener >= 0 && !t->menu) {
        c4fTesterMenuOpen(t, menuOpener);
        return 1;
    }
    if (!t->menu) {
        /* The remote is not under test, so it may leave and open the menu. */
        if (remotePressed & C4F_INPUT_BUTTON_CIRCLE) {
            c4fTesterClose(t);
            return 0;
        }
        if (remotePressed & (C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_OPTIONS)) {
            c4fTesterMenuOpen(t, -1);
        }
        return 1;
    }
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        const C4fTesterReader *r = &t->readers[s];
        if (r->state != C4F_TESTER_READY || !(r->sample.buttons & C4F_TESTER_STICKS)) t->menuOpeners &= ~(1u << s);
    }
    t->armed = !t->menuOpeners;
    uint32_t navigation = remotePressed & C4F_TESTER_DIRECTIONS;
    uint32_t pressed = t->armed ? remotePressed & C4F_TESTER_ACTIVATE : 0;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        C4fTesterReader *r = &t->readers[s];
        if (r->state != C4F_TESTER_READY) continue;
        r->menuBlocked &= r->sample.buttons;
        if (!t->armed) r->menuBlocked |= r->sample.buttons & C4F_TESTER_ACTIVATE;
        navigation |= c4fTesterMenuDirection(r, now);
        if (t->armed) pressed |= r->pressed & ~r->menuBlocked & C4F_TESTER_ACTIVATE;
    }
    if (t->colorMenu) {
        c4fTesterColorInput(t, navigation, pressed);
    } else if (pressed & C4F_INPUT_BUTTON_CIRCLE) {
        t->menu = 0;
    } else if (navigation) {
        c4fTesterMenuMove(t, navigation);
    } else if (pressed & C4F_INPUT_BUTTON_CROSS) {
        if (t->focus == C4F_TESTER_EXIT) {
            c4fTesterClose(t);
            return 0;
        }
        c4fTesterAct(t, t->focus, now);
    }
    return 1;
}

int c4fTesterItemAvailable(const C4fTesterView *v, int item)
{
    if (item < 0 || item >= C4F_TESTER_ITEMS) return 0;
    if (item == C4F_TESTER_RESUME || item == C4F_TESTER_EXIT) return 1;
    if (item == C4F_TESTER_USER) {
        for (int s = 0; s < C4F_TESTER_USERS; s++)
            if (s != v->selected && v->user[s].used && v->user[s].state != C4F_TESTER_SIGNED_OUT) return 1;
        return 0;
    }
    return v->selected >= 0 && v->selected < C4F_TESTER_USERS && v->user[v->selected].state == C4F_TESTER_READY;
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
    for (int s = 0; s < C4F_TESTER_USERS; s++) v->user[s].id = -1;
    if (!t->active) return;
    v->active = 1;
    v->menu = t->menu;
    v->focus = t->focus;
    v->colorMenu = t->colorMenu;
    v->colorChannel = t->colorChannel;
    v->colorFine = t->colorFine;
    v->selected = t->selected;
    for (int s = 0; s < C4F_TESTER_USERS; s++) {
        const C4fTesterReader *r = &t->readers[s];
        v->user[s].used = r->user >= 0;
        if (!v->user[s].used) continue;
        v->user[s].id = r->user;
        snprintf(v->user[s].name, sizeof(v->user[s].name), "%s", r->name);
        v->user[s].nameGenerated = r->nameGenerated;
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
    case C4F_TESTER_USER: return "Switch user";
    case C4F_TESTER_RUMBLE_LARGE: return "Rumble the large motor";
    case C4F_TESTER_RUMBLE_SMALL: return "Rumble the small motor";
    case C4F_TESTER_LIGHT_COLOR: return "Choose the light-bar color";
    case C4F_TESTER_LIGHT_RESET: return "Reset the light bar";
    case C4F_TESTER_ORIENTATION: return "Reset orientation";
    case C4F_TESTER_EXIT: return "Back to the launcher";
    default: return "";
    }
}
