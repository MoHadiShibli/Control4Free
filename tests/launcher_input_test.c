/* Launcher input and navigation using the real modules and simulated native
 * readers. No controller service, network or console is contacted. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "autorun.h"
#include "input.h"
#include "navigation.h"
#include "screen.h"

typedef struct {
    int handle, owned, openError, readError, connected;
    int opens, reads, closes;
    uint32_t buttons;
} FakeSource;

typedef struct {
    int initialResult, initialCalls;
    int32_t user;
    FakeSource source[2];
} FakeInput;

static int fakeInitial(void *context, int32_t *user)
{
    FakeInput *fake = context;
    fake->initialCalls++;
    *user = fake->user;
    return fake->initialResult;
}

static int fakeOpen(void *context, int32_t user, int port, int *owned)
{
    FakeInput *fake = context;
    int index;
    if (port == C4F_INPUT_PORT_STANDARD) {
        assert(user == fake->user && user >= 0);
        index = C4F_INPUT_STANDARD;
    } else {
        assert(port == C4F_INPUT_PORT_REMOTE && user == C4F_INPUT_SYSTEM_USER);
        index = C4F_INPUT_REMOTE;
    }
    FakeSource *source = &fake->source[index];
    source->opens++;
    *owned = source->owned;
    return source->openError ? -1 : source->handle;
}

static FakeSource *fakeHandle(FakeInput *fake, int handle)
{
    for (int i = 0; i < 2; i++) if (fake->source[i].handle == handle) return &fake->source[i];
    assert(!"unknown handle");
    return NULL;
}

static int fakeRead(void *context, int handle, uint32_t *buttons, int *connected)
{
    FakeSource *source = fakeHandle(context, handle);
    source->reads++;
    /* Poison even a failed sample: a failed native call must never leak it. */
    *buttons = source->buttons;
    *connected = source->connected;
    return source->readError ? -1 : 0;
}

static void fakeClose(void *context, int handle)
{
    FakeSource *source = fakeHandle(context, handle);
    assert(source->owned);
    source->closes++;
    assert(source->closes <= source->opens);
}

static void reader(FakeInput *fake, C4fInput *input)
{
    memset(fake, 0, sizeof(*fake));
    fake->user = 42;
    fake->source[C4F_INPUT_STANDARD] = (FakeSource){ .handle = 11, .owned = 1, .connected = 1 };
    fake->source[C4F_INPUT_REMOTE] = (FakeSource){ .handle = 22, .owned = 1, .connected = 1 };
    C4fInputOps ops = { fake, fakeInitial, fakeOpen, fakeRead, fakeClose };
    c4fInputInit(input, &ops);
}

static C4fInputFrame poll(C4fInput *input, uint64_t now)
{
    C4fInputFrame frame;
    /* Poll must fill both sources even if one cannot be opened. */
    memset(&frame, 0xff, sizeof(frame));
    c4fInputPoll(input, now, &frame);
    return frame;
}

static void remoteOnly(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    fake.source[C4F_INPUT_STANDARD].openError = 1;
    fake.source[C4F_INPUT_REMOTE].buttons = C4F_INPUT_BUTTON_CROSS;
    C4fInputFrame frame = poll(&input, 0);
    assert(!frame.standard.available && !frame.standard.buttons && !frame.standard.pressed);
    assert(frame.remote.available && !frame.remote.pressed);
    fake.source[C4F_INPUT_REMOTE].buttons = 0;
    poll(&input, 10);
    fake.source[C4F_INPUT_REMOTE].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 20);
    assert(frame.remote.pressed == C4F_INPUT_BUTTON_CROSS);
    assert(!poll(&input, 30).remote.pressed);
    assert(fake.source[C4F_INPUT_STANDARD].opens == 1);
    c4fInputClose(&input);
    assert(!fake.source[C4F_INPUT_STANDARD].closes && fake.source[C4F_INPUT_REMOTE].closes == 1);
    c4fInputClose(&input);
    assert(fake.source[C4F_INPUT_REMOTE].closes == 1);
    puts("PASS remote-only launch, held OK baseline, no repeated activation and owned-handle cleanup");
}

static void sourceEdges(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    C4fInputFrame frame = poll(&input, 0);
    assert(frame.standard.available && frame.remote.available);
    fake.source[C4F_INPUT_STANDARD].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 10);
    assert(frame.standard.pressed == C4F_INPUT_BUTTON_CROSS && !frame.remote.pressed);
    fake.source[C4F_INPUT_REMOTE].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 20);
    assert(!frame.standard.pressed && frame.remote.pressed == C4F_INPUT_BUTTON_CROSS);
    fake.source[C4F_INPUT_STANDARD].buttons = 0;
    frame = poll(&input, 30);
    assert(!frame.standard.buttons && frame.remote.buttons == C4F_INPUT_BUTTON_CROSS);
    assert(!frame.standard.pressed && !frame.remote.pressed);
    fake.source[C4F_INPUT_STANDARD].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 40);
    assert(frame.standard.pressed == C4F_INPUT_BUTTON_CROSS && !frame.remote.pressed);
    fake.source[C4F_INPUT_REMOTE].buttons = 0;
    poll(&input, 50);
    fake.source[C4F_INPUT_REMOTE].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 60);
    assert(!frame.standard.pressed && frame.remote.pressed == C4F_INPUT_BUTTON_CROSS);
    c4fInputClose(&input);
    assert(fake.source[0].closes == 1 && fake.source[1].closes == 1);
    puts("PASS independent controller/remote edges while either source holds or releases the same button");
}

static void remoteOpenRetry(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    fake.source[1].openError = 1;
    C4fInputFrame frame = poll(&input, 0);
    assert(frame.standard.available && !frame.remote.available);
    fake.source[0].buttons = C4F_INPUT_BUTTON_CROSS;
    frame = poll(&input, 10);
    assert(frame.standard.pressed == C4F_INPUT_BUTTON_CROSS && !frame.remote.buttons);
    fake.source[1].openError = 0;
    fake.source[1].buttons = C4F_INPUT_BUTTON_CROSS;
    poll(&input, 999);
    assert(fake.source[1].opens == 1);
    frame = poll(&input, 1000);
    assert(frame.remote.available && !frame.remote.buttons && !frame.remote.pressed);
    assert(fake.source[1].opens == 2 && !frame.standard.pressed);
    fake.source[1].buttons = 0;
    poll(&input, 1010);
    fake.source[1].buttons = C4F_INPUT_BUTTON_CROSS;
    assert(poll(&input, 1020).remote.pressed == C4F_INPUT_BUTTON_CROSS);
    c4fInputClose(&input);
    assert(fake.source[0].closes == 1 && fake.source[1].closes == 1);
    puts("PASS unavailable remote leaves DS4 operational and retry waits for a new neutral baseline");
}

static void readerRecovery(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    poll(&input, 0);
    fake.source[0].buttons = C4F_INPUT_BUTTON_CROSS;
    fake.source[1].buttons = C4F_INPUT_BUTTON_RIGHT;
    poll(&input, 10);
    fake.source[1].readError = 1;
    C4fInputFrame frame = poll(&input, 20);
    assert(frame.standard.available && frame.standard.buttons == C4F_INPUT_BUTTON_CROSS);
    assert(!frame.remote.available && !frame.remote.buttons && !frame.remote.pressed);
    assert(fake.source[1].closes == 1);
    fake.source[1].readError = 0;
    poll(&input, 1019);
    assert(fake.source[1].opens == 1);
    frame = poll(&input, 1020);
    assert(fake.source[1].opens == 2 && frame.remote.available && !frame.remote.pressed);
    fake.source[1].buttons = 0;
    poll(&input, 1030);
    fake.source[1].buttons = C4F_INPUT_BUTTON_RIGHT;
    assert(poll(&input, 1040).remote.pressed == C4F_INPUT_BUTTON_RIGHT);
    fake.source[0].connected = 0;
    frame = poll(&input, 1050);
    assert(!frame.standard.available && !frame.standard.buttons && !frame.standard.pressed);
    assert(frame.remote.available && frame.remote.buttons == C4F_INPUT_BUTTON_RIGHT);
    assert(fake.source[0].closes == 1);
    fake.source[0].connected = 1;
    poll(&input, 2049);
    assert(fake.source[0].opens == 1);
    frame = poll(&input, 2050);
    assert(frame.standard.available && !frame.standard.pressed && fake.source[0].opens == 2);
    fake.source[0].buttons = 0;
    poll(&input, 2060);
    fake.source[0].buttons = C4F_INPUT_BUTTON_CROSS;
    assert(poll(&input, 2070).standard.pressed == C4F_INPUT_BUTTON_CROSS);
    c4fInputReset(&input, 2080);
    frame = poll(&input, 2080);
    assert(!frame.standard.pressed && !frame.remote.pressed);
    fake.source[0].buttons = fake.source[1].buttons = 0;
    poll(&input, 4000);
    fake.source[0].buttons = C4F_INPUT_BUTTON_CROSS;
    assert(poll(&input, 4010).standard.pressed == C4F_INPUT_BUTTON_CROSS);
    c4fInputClose(&input);
    assert(fake.source[0].closes == fake.source[0].opens);
    assert(fake.source[1].closes == fake.source[1].opens);
    puts("PASS read failures/disconnects clear stale activity, bounded retry and neutral-gated recovery/reset");
}

static void unavailableUserAndBorrowedHandle(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    fake.initialResult = -1;
    C4fInputFrame frame = poll(&input, 0);
    assert(!frame.standard.available && frame.remote.available);
    assert(fake.initialCalls == 1 && !fake.source[0].opens);
    fake.initialResult = 0;
    fake.user = -1;
    poll(&input, 1000);
    assert(fake.initialCalls == 2 && !fake.source[0].opens);
    fake.user = 42;
    fake.source[0].owned = 0; /* get-handle result owned by another subsystem */
    frame = poll(&input, 2000);
    assert(frame.standard.available && fake.source[0].opens == 1);
    fake.source[0].readError = 1;
    frame = poll(&input, 2010);
    assert(!frame.standard.available && !frame.standard.buttons);
    assert(!fake.source[0].closes && frame.remote.available);
    fake.source[0].readError = 0;
    frame = poll(&input, 3010);
    assert(frame.standard.available && fake.source[0].opens == 2);
    c4fInputClose(&input);
    assert(!fake.source[0].closes && fake.source[1].closes == 1);
    puts("PASS unavailable/invalid starting user does not block remote, borrowed handles remain open");
}

static C4fLauncherScreen stopped(void)
{
    C4fLauncherScreen screen;
    memset(&screen, 0, sizeof(screen));
    screen.autorun = C4F_AUTORUN_ON;
    return screen;
}

static int navigate(C4fLauncherScreen *screen, uint32_t standard, uint32_t remote, int diagnostic)
{
    uint32_t standardUnhandled = 0, remoteUnhandled = 0;
    return c4fNavigate(screen, standard, remote, diagnostic, &standardUnhandled, &remoteUnhandled);
}

static int remoteFrame(FakeInput *fake, C4fInput *input, C4fLauncherScreen *screen,
                       uint64_t now, uint32_t buttons)
{
    fake->source[1].buttons = buttons;
    C4fInputFrame frame = poll(input, now);
    return navigate(screen, frame.standard.pressed, frame.remote.pressed, 0);
}

static void heldRemoteConfirmation(void)
{
    FakeInput fake;
    C4fInput input;
    reader(&fake, &input);
    fake.source[0].openError = 1;
    C4fLauncherScreen screen = stopped();
    screen.running = 1;
    assert(remoteFrame(&fake, &input, &screen, 0, 0) == C4F_ACTION_NONE);
    assert(remoteFrame(&fake, &input, &screen, 10, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_NONE);
    assert(screen.actionMenu);
    assert(remoteFrame(&fake, &input, &screen, 20, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_NONE);
    assert(screen.actionMenu);
    remoteFrame(&fake, &input, &screen, 30, 0);
    remoteFrame(&fake, &input, &screen, 40, C4F_INPUT_BUTTON_UP);
    remoteFrame(&fake, &input, &screen, 50, 0);
    remoteFrame(&fake, &input, &screen, 60, C4F_INPUT_BUTTON_UP);
    assert(screen.actionFocus == C4F_ACTION_STOP);
    remoteFrame(&fake, &input, &screen, 70, 0);
    assert(remoteFrame(&fake, &input, &screen, 80, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_NONE);
    assert(screen.confirmStop && !screen.confirmChoice);
    assert(remoteFrame(&fake, &input, &screen, 90, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_NONE);
    assert(screen.confirmStop && !screen.confirmChoice);
    remoteFrame(&fake, &input, &screen, 100, 0);
    remoteFrame(&fake, &input, &screen, 110, C4F_INPUT_BUTTON_RIGHT);
    assert(screen.confirmChoice);
    remoteFrame(&fake, &input, &screen, 120, 0);
    assert(remoteFrame(&fake, &input, &screen, 130, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_STOP);
    assert(remoteFrame(&fake, &input, &screen, 140, C4F_INPUT_BUTTON_CROSS) == C4F_ACTION_NONE);
    c4fInputClose(&input);
    puts("PASS combined native-reader/navigation flow never selects or confirms Stop from held OK");
}

static void menuActions(void)
{
    C4fLauncherScreen screen = stopped();
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_START);
    assert(navigate(&screen, 0, 0, 0) == C4F_ACTION_NONE && screen.actionMenu);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_START);
    assert(!screen.actionMenu);

    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_DOWN, 0) == C4F_ACTION_NONE);
    assert(screen.actionFocus == C4F_ACTION_AUTORUN);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_AUTORUN);
    assert(!screen.actionMenu);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0) == C4F_ACTION_NONE);
    assert(screen.actionFocus == C4F_ACTION_RESUME);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_RESUME);
    assert(!screen.actionMenu);

    screen.running = 1;
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_RESUME);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0) == C4F_ACTION_NONE);
    assert(screen.actionFocus == C4F_ACTION_CLOSE);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_CLOSE);
    assert(!screen.actionMenu);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CIRCLE, 0) == C4F_ACTION_CLOSE);
    screen = stopped();
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CIRCLE, 0) == C4F_ACTION_RESUME);
    assert(!screen.actionMenu);
    puts("PASS remote arrows/OK reach Start, Auto-start, Resume and Close; opening OK is consumed");
}

static void stopConfirmation(void)
{
    C4fLauncherScreen screen = stopped();
    screen.running = 1;
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0);
    navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0);
    assert(screen.actionFocus == C4F_ACTION_STOP);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.confirmStop && screen.confirmMenu && !screen.confirmChoice && !screen.actionMenu);
    assert(navigate(&screen, 0, 0, 0) == C4F_ACTION_NONE && screen.confirmStop);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_RESUME);
    assert(!screen.confirmStop);
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0);
    navigate(&screen, 0, C4F_INPUT_BUTTON_UP, 0);
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_RIGHT, 0) == C4F_ACTION_NONE);
    assert(screen.confirmChoice);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_STOP);
    assert(!screen.confirmStop);

    /* Existing DS4 shortcuts keep their separate confirmation flow. */
    assert(navigate(&screen, C4F_INPUT_BUTTON_SQUARE, 0, 0) == C4F_ACTION_NONE);
    assert(screen.confirmStop && !screen.confirmMenu);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS, 0, 0) == C4F_ACTION_STOP);
    assert(!screen.confirmStop);
    navigate(&screen, C4F_INPUT_BUTTON_SQUARE, 0, 0);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CIRCLE, 0, 0) == C4F_ACTION_RESUME);
    assert(!screen.confirmStop);
    navigate(&screen, C4F_INPUT_BUTTON_SQUARE, 0, 0);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.confirmStop && screen.confirmMenu && !screen.confirmChoice);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_RESUME);
    assert(!screen.confirmStop);
    navigate(&screen, C4F_INPUT_BUTTON_SQUARE, 0, 0);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.confirmStop && screen.confirmMenu && !screen.confirmChoice);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS, 0, 0) == C4F_ACTION_RESUME);
    navigate(&screen, C4F_INPUT_BUTTON_SQUARE, 0, 0);
    assert(navigate(&screen, C4F_INPUT_BUTTON_LEFT, 0, 0) == C4F_ACTION_NONE);
    assert(screen.confirmStop && screen.confirmMenu && !screen.confirmChoice);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS, 0, 0) == C4F_ACTION_RESUME);
    puts("PASS remote Stop requires a separate fresh confirmation, defaults Cancel and preserves DS4 shortcuts");
}

static void unavailableActions(void)
{
    C4fLauncherScreen screen = stopped();
    screen.autorun = C4F_AUTORUN_UNKNOWN;
    assert(!c4fActionAvailable(&screen, C4F_ACTION_AUTORUN, 0));
    assert(navigate(&screen, C4F_INPUT_BUTTON_TRIANGLE, 0, 0) == C4F_ACTION_NONE);
    screen.locked = 1;
    assert(!c4fActionAvailable(&screen, C4F_ACTION_START, 0));
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS, 0, 0) == C4F_ACTION_NONE);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CIRCLE, 0, 0) == C4F_ACTION_CLOSE);
    screen.busy = 1;
    assert(navigate(&screen, C4F_INPUT_BUTTON_CROSS | C4F_INPUT_BUTTON_TRIANGLE | C4F_INPUT_BUTTON_SQUARE,
                    C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_RESUME);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CIRCLE, C4F_INPUT_BUTTON_CIRCLE, 0) == C4F_ACTION_RESUME);
    assert(navigate(&screen, C4F_INPUT_BUTTON_CIRCLE, C4F_INPUT_BUTTON_CIRCLE, 0) == C4F_ACTION_NONE);
    screen = stopped();
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    assert(screen.actionFocus == C4F_ACTION_START);
    screen.running = 1; /* a status refresh while the user is choosing */
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus != C4F_ACTION_START);
    /* A direction and OK arriving together must only move the focus. */
    screen = stopped();
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 0);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0) == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_AUTORUN);
    assert(navigate(&screen, C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0)
           == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_AUTORUN);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0)
           == C4F_ACTION_NONE);
    assert(screen.actionMenu && screen.actionFocus == C4F_ACTION_AUTORUN);
    screen.running = 1;
    screen.actionMenu = 0;
    screen.confirmStop = screen.confirmMenu = screen.confirmChoice = 1;
    assert(navigate(&screen, C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_DOWN | C4F_INPUT_BUTTON_CROSS, 0)
           == C4F_ACTION_NONE);
    assert(screen.confirmStop && screen.confirmChoice);
    puts("PASS unavailable/busy actions, status-change focus repair and simultaneous direction/OK safety");
}

static void diagnosticNavigation(void)
{
    C4fLauncherScreen screen = stopped();
    uint32_t standardUnhandled, remoteUnhandled;
    uint32_t directions = C4F_INPUT_BUTTON_UP | C4F_INPUT_BUTTON_RIGHT;
    assert(c4fNavigate(&screen, directions, directions, 1, &standardUnhandled, &remoteUnhandled) == C4F_ACTION_NONE);
    assert(standardUnhandled == directions && remoteUnhandled == directions);
    assert(!screen.actionMenu);
    assert(c4fNavigate(&screen, C4F_INPUT_BUTTON_OPTIONS, 0, 1,
                       &standardUnhandled, &remoteUnhandled) == C4F_ACTION_NONE);
    assert(standardUnhandled == C4F_INPUT_BUTTON_OPTIONS && !remoteUnhandled);
    assert(!c4fActionAvailable(&screen, C4F_ACTION_CAPTURE, 0));
    assert(c4fActionAvailable(&screen, C4F_ACTION_CAPTURE, 1));
    navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 1);
    /* The action list defines display order; navigating through every row
     * ensures capture is reachable using only the TV remote. */
    C4fLauncherAction actions[8];
    int count = c4fActionList(&screen, 1, actions, 8), reached = 0;
    assert(count > 0 && count <= 8);
    for (int i = 0; i <= count; i++) {
        if (screen.actionFocus == C4F_ACTION_CAPTURE) { reached = 1; break; }
        assert(c4fNavigate(&screen, 0, C4F_INPUT_BUTTON_DOWN, 1,
                           &standardUnhandled, &remoteUnhandled) == C4F_ACTION_NONE);
        assert(!remoteUnhandled);
    }
    assert(reached);
    assert(navigate(&screen, 0, C4F_INPUT_BUTTON_CROSS, 1) == C4F_ACTION_CAPTURE);
    assert(!screen.actionMenu);
    puts("PASS diagnostic arrows remain page/scroll inputs outside Actions; remote can choose Capture");
}

int main(void)
{
    remoteOnly();
    sourceEdges();
    remoteOpenRetry();
    readerRecovery();
    unavailableUserAndBorrowedHandle();
    menuActions();
    heldRemoteConfirmation();
    stopConfirmation();
    unavailableActions();
    diagnosticNavigation();
    return 0;
}
