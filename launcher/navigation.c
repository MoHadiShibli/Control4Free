#include "navigation.h"
#include "autorun.h"
#include "input.h"

#define C4F_NAV_BACK C4F_INPUT_BUTTON_CIRCLE
#define C4F_NAV_OK C4F_INPUT_BUTTON_CROSS
#define C4F_NAV_PREVIOUS (C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_LEFT)
#define C4F_NAV_NEXT (C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_RIGHT)
#define C4F_NAV_DIRECTIONS (C4F_NAV_PREVIOUS | C4F_NAV_NEXT)

int c4fActionAvailable(const C4fLauncherScreen *screen, C4fLauncherAction action, int diagnostic)
{
    switch (action) {
    case C4F_ACTION_RESUME: return 1;
    case C4F_ACTION_START: return !screen->busy && !screen->locked && screen->running != 1;
    case C4F_ACTION_AUTORUN:
        return !screen->busy && !screen->locked && screen->autorun != C4F_AUTORUN_UNKNOWN;
    case C4F_ACTION_STOP: return !screen->busy && !screen->locked && screen->running == 1;
    case C4F_ACTION_CAPTURE: return !!diagnostic;
    case C4F_ACTION_CLOSE: return !screen->busy;
    default: return 0;
    }
}

int c4fActionList(const C4fLauncherScreen *screen, int diagnostic,
                 C4fLauncherAction *actions, int capacity)
{
    const C4fLauncherAction order[] = { C4F_ACTION_RESUME, C4F_ACTION_START,
        C4F_ACTION_AUTORUN, C4F_ACTION_STOP, C4F_ACTION_CAPTURE, C4F_ACTION_CLOSE };
    int count = 0;
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (!c4fActionAvailable(screen, order[i], diagnostic)) continue;
        if (actions && count < capacity) actions[count] = order[i];
        count++;
    }
    return count;
}

const char *c4fActionLabel(const C4fLauncherScreen *screen, C4fLauncherAction action)
{
    switch (action) {
    case C4F_ACTION_RESUME: return "Resume";
    case C4F_ACTION_START: return screen->autorun == C4F_AUTORUN_ON ? "Start Control4Free" : "Set up and start";
    case C4F_ACTION_AUTORUN:
        if (screen->autorun == C4F_AUTORUN_ON) return "Turn auto-start off";
        if (screen->autorun == C4F_AUTORUN_OUTDATED) return "Update auto-start";
        return "Turn auto-start on";
    case C4F_ACTION_STOP: return "Stop Control4Free";
    case C4F_ACTION_CLOSE: return "Close app";
    case C4F_ACTION_CAPTURE: return "Capture kernel log";
    default: return "";
    }
}

static C4fLauncherAction c4fDefaultAction(const C4fLauncherScreen *screen, int diagnostic)
{
    return c4fActionAvailable(screen, C4F_ACTION_START, diagnostic) ? C4F_ACTION_START : C4F_ACTION_RESUME;
}

static int c4fDirection(uint32_t buttons)
{
    int previous = !!(buttons & C4F_NAV_PREVIOUS), next = !!(buttons & C4F_NAV_NEXT);
    return previous == next ? 0 : next ? 1 : -1;
}

static void c4fConsume(uint32_t *standardUnhandled, uint32_t *remoteUnhandled)
{
    if (standardUnhandled) *standardUnhandled = 0;
    if (remoteUnhandled) *remoteUnhandled = 0;
}

static void c4fCancelConfirmation(C4fLauncherScreen *screen)
{
    screen->confirmStop = 0;
    screen->confirmChoice = 0;
    screen->confirmMenu = 0;
}

C4fLauncherAction c4fNavigate(C4fLauncherScreen *screen, uint32_t standardPressed,
                             uint32_t remotePressed, int diagnostic,
                             uint32_t *standardUnhandled, uint32_t *remoteUnhandled)
{
    if (standardUnhandled) *standardUnhandled = standardPressed;
    if (remoteUnhandled) *remoteUnhandled = remotePressed;
    uint32_t pressed = standardPressed | remotePressed;
    if (screen->confirmStop) {
        c4fConsume(standardUnhandled, remoteUnhandled);
        if (!c4fActionAvailable(screen, C4F_ACTION_STOP, diagnostic)) {
            c4fCancelConfirmation(screen);
            return C4F_ACTION_NONE;
        }
        if (pressed & C4F_NAV_BACK) {
            c4fCancelConfirmation(screen);
            return C4F_ACTION_RESUME;
        }
        /* The DS4 shortcut displays Cross-to-stop. A remote's first event
         * instead reveals an explicit, Cancel-focused selection and cannot
         * accept the confirmation that another source opened. */
        if (!screen->confirmMenu && remotePressed) {
            screen->confirmMenu = 1;
            screen->confirmChoice = 0;
            return C4F_ACTION_NONE;
        }
        if (pressed & C4F_NAV_DIRECTIONS) {
            if (!screen->confirmMenu) {
                screen->confirmMenu = 1;
                screen->confirmChoice = 0;
            }
            int direction = c4fDirection(pressed);
            if (direction) screen->confirmChoice = direction > 0;
            return C4F_ACTION_NONE;
        }
        if (pressed & C4F_NAV_OK) {
            int stop = screen->confirmChoice;
            c4fCancelConfirmation(screen);
            return stop ? C4F_ACTION_STOP : C4F_ACTION_RESUME;
        }
        return C4F_ACTION_NONE;
    }
    if (screen->actionMenu) {
        c4fConsume(standardUnhandled, remoteUnhandled);
        if (!c4fActionAvailable(screen, (C4fLauncherAction)screen->actionFocus, diagnostic)) {
            screen->actionFocus = c4fDefaultAction(screen, diagnostic);
            return C4F_ACTION_NONE;
        }
        if (pressed & C4F_NAV_BACK) {
            screen->actionMenu = 0;
            return C4F_ACTION_RESUME;
        }
        if (pressed & C4F_NAV_DIRECTIONS) {
            int direction = c4fDirection(pressed);
            if (direction) {
                C4fLauncherAction actions[6];
                int count = c4fActionList(screen, diagnostic, actions, 6), at = 0;
                while (at < count && (int)actions[at] != screen->actionFocus) at++;
                screen->actionFocus = actions[(at + count + direction) % count];
            }
            return C4F_ACTION_NONE;
        }
        if (pressed & C4F_NAV_OK) {
            C4fLauncherAction action = (C4fLauncherAction)screen->actionFocus;
            screen->actionMenu = 0;
            if (action == C4F_ACTION_STOP) {
                screen->confirmStop = 1;
                screen->confirmChoice = 0;
                screen->confirmMenu = 1;
                return C4F_ACTION_NONE;
            }
            return action;
        }
        return C4F_ACTION_NONE;
    }
    /* A TV remote needs an action menu because it has no Triangle or Square.
     * In diagnostic mode its arrows keep the existing paging and scrolling. */
    if ((remotePressed & (C4F_NAV_OK | C4F_INPUT_BUTTON_OPTIONS)) ||
        (!diagnostic && (pressed & C4F_NAV_DIRECTIONS))) {
        c4fConsume(standardUnhandled, remoteUnhandled);
        screen->actionMenu = 1;
        screen->actionFocus = c4fDefaultAction(screen, diagnostic);
        return C4F_ACTION_NONE;
    }
    if (screen->busy) return C4F_ACTION_NONE;
    if (pressed & C4F_NAV_BACK) {
        c4fConsume(standardUnhandled, remoteUnhandled);
        return C4F_ACTION_CLOSE;
    }
    if (standardPressed & C4F_NAV_OK)
        return c4fActionAvailable(screen, C4F_ACTION_START, diagnostic) ? C4F_ACTION_START : C4F_ACTION_NONE;
    if ((standardPressed & C4F_INPUT_BUTTON_TRIANGLE) &&
        c4fActionAvailable(screen, C4F_ACTION_AUTORUN, diagnostic)) return C4F_ACTION_AUTORUN;
    if ((standardPressed & C4F_INPUT_BUTTON_SQUARE) && c4fActionAvailable(screen, C4F_ACTION_STOP, diagnostic)) {
        screen->confirmStop = 1;
        screen->confirmChoice = 1;
        screen->confirmMenu = 0;
        c4fConsume(standardUnhandled, remoteUnhandled);
    }
    return C4F_ACTION_NONE;
}
