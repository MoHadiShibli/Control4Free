/* The real sampler and visible-state comparison from main.cpp, with only native
 * APIs replaced. The display thread never polls input in these scenarios. */
#include <assert.h>
#include <atomic>
#include <chrono>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include "autorun.h"
#include "input.h"
#include "navigation.h"
#include "screen.h"
#include "service.h"

static pthread_mutex_t c4fMutex = PTHREAD_MUTEX_INITIALIZER;
static C4fLauncherScreen c4fScreen;
static int c4fCommand, c4fQuit, c4fCloseRequested;
static uint64_t c4fRenderAt, c4fInputAt;
/* Native API context is opaque to this test; its handles are owned by sampler. */
typedef struct { int handles[2]; } C4fNativeInput;
static std::atomic<uint32_t> standardButtons{0}, remoteButtons{0};
static std::atomic<int> polls{0}, closes{0}, largeMotor{0};
static std::atomic<bool> standardOpen{false}, lightOn{false};
static std::atomic<bool> blockRead{false}, readBlocked{false}, releaseRead{false};

uint64_t c4fLauncherTimeMs(void)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int c4fNativeInitialUser(void *, int32_t *user) { *user = 1; return 0; }
static int c4fNativeOpen(void *, int32_t, int port, int *owned)
{
    *owned = 1;
    if (port != 16) standardOpen = true;
    return port == 16 ? 11 : 10;
}
static int c4fNativeRead(void *, int handle, uint32_t *buttons, int *connected)
{
    polls++;
    *buttons = handle == 10 ? standardButtons.load() : remoteButtons.load();
    *connected = 1;
    return 0;
}
static void c4fNativeClose(void *, int handle)
{
    if (handle == 10) {
        assert(largeMotor.load() == 0 && !lightOn.load());
        standardOpen = false;
    }
    closes++;
}
static int c4fTesterLoginUsers(void *, int32_t *users) { users[0] = 1; return 1; }
static void c4fTesterUserName(void *, int32_t, char *name, int size) { snprintf(name, size, "Alex"); }
static int c4fTesterOpenPad(void *, int32_t, int *owned) { *owned = 0; return 10; }
static int c4fTesterRead(void *, int, C4fTesterSample *sample)
{
    if (blockRead.exchange(false)) {
        readBlocked = true;
        while (!releaseRead) usleep(1000);
        readBlocked = false;
    }
    memset(sample, 0, sizeof(*sample));
    sample->buttons = standardButtons.load(); sample->connected = 1;
    sample->lx = sample->ly = sample->rx = sample->ry = 128;
    return 0;
}
static void c4fTesterClosePad(void *, int) { assert(!"borrowed handle closed"); }
static int c4fTesterVibrate(void *, int, uint8_t large, uint8_t)
{
    assert(standardOpen.load());
    largeMotor = large;
    return 0;
}
static int c4fTesterLight(void *, int, uint8_t, uint8_t, uint8_t) { assert(standardOpen.load()); lightOn = true; return 0; }
static int c4fTesterResetLight(void *, int) { assert(standardOpen.load()); lightOn = false; return 0; }
static int c4fTesterResetOrientation(void *, int) { return 0; }
static int c4fTesterMotion(void *, int, int) { return 0; }
static void c4fTesterLogState(void *, int, int, uint32_t) {}
static void c4fNativeInputNote(const C4fNativeInput *, char *note, size_t size) { snprintf(note, size, "test input"); }

#include "launcher-sampling-source.inc"

template <typename Predicate> static void until(Predicate predicate)
{
    uint64_t deadline = c4fLauncherTimeMs() + 1000;
    while (!predicate()) { assert(c4fLauncherTimeMs() < deadline); usleep(1000); }
}

static C4fTesterView view()
{
    pthread_mutex_lock(&c4fMutex);
    C4fTesterView result = c4fScreen.tester;
    pthread_mutex_unlock(&c4fMutex);
    return result;
}

static void tap(std::atomic<uint32_t> &buttons, uint32_t mask)
{
    buttons = mask; usleep(16000); buttons = 0; usleep(16000);
}

static void noConnectionPads(C4fTesterConnection connection, int available)
{
    assert(connection.available == available && connection.pads == 0);
    for (int p = 0; p < C4F_SERVICE_PADS; p++) assert(connection.rttMs[p] == -1);
}

static void connectionView()
{
    C4fServiceStatus status = {};
    status.latencyAvailable = 1;
    C4fTesterView tester = {};
    tester.active = 1; tester.selected = 2;
    tester.user[0].used = tester.user[2].used = 1;
    tester.user[0].state = tester.user[2].state = C4F_TESTER_READY;
    tester.user[0].id = 1; tester.user[2].id = 0x1A2B3C4D;
    snprintf(tester.user[0].name, sizeof(tester.user[0].name), "Alex");
    snprintf(tester.user[2].name, sizeof(tester.user[2].name), "Alex");
    status.latency[0] = { 1, 1, 4, 0 };
    status.latency[1] = { 1, 0x1A2B3C4D, 8, 100 };
    status.latency[2] = { 0, 0x1A2B3C4D, 0, 0 };
    status.latency[3] = { 1, 0x1A2B3C4D, 12, 200 };
    const uint64_t checkedAt = 10000;
    C4fTesterConnection connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester);
    assert(connection.available && connection.pads == ((1U << 1) | (1U << 3)));
    assert(connection.rttMs[0] == -1 && connection.rttMs[1] == 8 && connection.rttMs[2] == -1 && connection.rttMs[3] == 12);
    /* Equal display names and equal slot indexes never establish ownership. */
    tester.selected = 0;
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester);
    assert(connection.pads == 1 && connection.rttMs[0] == 4 && connection.rttMs[1] == -1 && connection.rttMs[3] == -1);
    tester.selected = 2;
    tester.user[2].id = 99;
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 1);
    tester.user[2].id = 0x1A2B3C4D;
    printf("PASS connection view: selected user ID matches every owned virtual slot, independently of names and indexes\n");

    /* The cached service reply also ages while the launcher renders it. */
    status.latency[1].ageMs = C4F_SERVICE_LATENCY_STALE_MS - 1000;
    status.latency[3].ageMs = C4F_SERVICE_LATENCY_STALE_MS - 999;
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt + 1000, &tester);
    assert(connection.pads == ((1U << 1) | (1U << 3)) && connection.rttMs[1] == 8 && connection.rttMs[3] == -1);
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt + 1001, &tester);
    assert(connection.pads == ((1U << 1) | (1U << 3)) && connection.rttMs[1] == -1 && connection.rttMs[3] == -1);
    status.latency[1].ageMs = status.latency[3].ageMs = 0;
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt + C4F_SERVICE_LATENCY_STALE_MS, &tester);
    assert(connection.available && connection.rttMs[1] == 8 && connection.rttMs[3] == 12);
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt,
                     checkedAt + C4F_SERVICE_LATENCY_STALE_MS + 1, &tester), 0);
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt - 1, &tester), 0);
    printf("PASS connection view: each sample expires at its own age plus local cache age; stale or future cache is unavailable\n");

    status.latency[1].rttMs = -1;
    status.latency[3].ageMs = -1;
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester);
    assert(connection.available && connection.pads == ((1U << 1) | (1U << 3)));
    for (int p = 0; p < C4F_SERVICE_PADS; p++) assert(connection.rttMs[p] == -1);
    status.latency[1].rttMs = 0;
    connection = c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester);
    assert(connection.rttMs[1] == 0 && connection.rttMs[3] == -1);
    const int serviceStates[] = { C4F_SERVICE_UNKNOWN, C4F_SERVICE_DOWN };
    for (int running : serviceStates)
        noConnectionPads(c4fTesterConnectionView(&status, running, checkedAt, checkedAt, &tester), 0);
    status.latencyAvailable = 0; /* A service without telemetry support. */
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 0);
    status.latencyAvailable = 1;
    printf("PASS connection view: unknown RTT stays -1, measured zero stays zero, unknown/stopped/older service has no readings\n");

    tester.selected = -1;
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 1);
    tester.selected = C4F_TESTER_USERS;
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 1);
    tester.selected = 2; tester.user[2].used = 0;
    noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 1);
    tester.user[2].used = 1;
    const int readerStates[] = { C4F_TESTER_OPENING, C4F_TESTER_NO_CONTROLLER, C4F_TESTER_FAILED, C4F_TESTER_SIGNED_OUT };
    for (int state : readerStates) {
        tester.user[2].state = state;
        noConnectionPads(c4fTesterConnectionView(&status, C4F_SERVICE_RUNNING, checkedAt, checkedAt, &tester), 1);
    }
    printf("PASS connection view: no selected ready user means no virtual-controller association\n");
}

int main()
{
    connectionView();
    c4fScreen.running = 1; c4fScreen.autorun = C4F_AUTORUN_ON;
    c4fRenderAt = c4fLauncherTimeMs();
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, c4fInputWorker, NULL));
    until([] { return polls.load() >= 4; });
    tap(standardButtons, C4F_INPUT_BUTTON_OPTIONS);
    until([] { C4fTesterView v = view(); return v.active && v.user[0].state == C4F_TESTER_READY; });
    tap(remoteButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return view().menu; });
    std::thread press([] {
        usleep(20000); standardButtons = C4F_INPUT_BUTTON_DOWN;
        usleep(16000); standardButtons = 0;
    });
    int before = polls;
    usleep(100000); /* A display stall cannot swallow this complete short tap. */
    press.join();
    assert(view().focus == C4F_TESTER_RUMBLE_LARGE);
    assert(polls.load() - before >= 8);
    printf("PASS sampler: a short navigation tap survives a 100 ms display pause\n");

    assert(view().focus == C4F_TESTER_RUMBLE_LARGE);
    tap(standardButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return largeMotor.load() == 255; });
    blockRead = true;
    until([] { return readBlocked.load(); });
    assert(!pthread_mutex_trylock(&c4fMutex));
    c4fScreen.controllers = 3;
    pthread_mutex_unlock(&c4fMutex);
    releaseRead = true;
    until([] { return !readBlocked.load(); });
    printf("PASS sampler: blocked native reads leave the display/status lock available\n");

    // Keep the display alive while the input thread stalls. This must exercise
    // the input-gap resume branch, not the stale-display pause branch. Both
    // outputs use a handle borrowed from the ordinary launcher input reader.
    tap(standardButtons, C4F_INPUT_BUTTON_UP);
    tap(standardButtons, C4F_INPUT_BUTTON_LEFT);
    assert(view().focus == C4F_TESTER_LIGHT_COLOR);
    tap(standardButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return lightOn.load(); });
    tap(standardButtons, C4F_INPUT_BUTTON_CIRCLE);
    tap(standardButtons, C4F_INPUT_BUTTON_DOWN);
    tap(standardButtons, C4F_INPUT_BUTTON_RIGHT);
    assert(view().focus == C4F_TESTER_RUMBLE_LARGE);
    tap(standardButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return largeMotor.load() == 255; });
    releaseRead = false;
    blockRead = true;
    until([] { return readBlocked.load(); });
    int beforeResume = closes.load();
    for (int i = 0; i < 12; i++) {
        pthread_mutex_lock(&c4fMutex);
        c4fRenderAt = c4fLauncherTimeMs();
        pthread_mutex_unlock(&c4fMutex);
        usleep(100000);
    }
    pthread_mutex_lock(&c4fMutex);
    c4fRenderAt = c4fLauncherTimeMs();
    pthread_mutex_unlock(&c4fMutex);
    releaseRead = true;
    until([beforeResume] { return closes.load() >= beforeResume + 2 && !view().menu &&
                                view().user[0].state == C4F_TESTER_READY; });
    assert(!largeMotor.load() && !lightOn.load());
    printf("PASS sampler: fresh-display input-gap resume quiets borrowed-handle outputs before closing their owner\n");

    // Restore active output so the independent stale-display safety check
    // below cannot pass merely because the resume case already stopped it.
    tap(remoteButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return view().menu; });
    tap(standardButtons, C4F_INPUT_BUTTON_DOWN);
    assert(view().focus == C4F_TESTER_RUMBLE_LARGE);
    tap(standardButtons, C4F_INPUT_BUTTON_CROSS);
    until([] { return largeMotor.load() == 255; });
    int beforeDisplayPause = closes.load();

    pthread_mutex_lock(&c4fMutex);
    c4fRenderAt = c4fLauncherTimeMs() - 1100;
    pthread_mutex_unlock(&c4fMutex);
    until([] { return largeMotor.load() == 0 && view().menu == 0; });
    assert(closes.load() >= beforeDisplayPause + 2);
    pthread_mutex_lock(&c4fMutex);
    assert(c4fScreen.controllers == 3);
    c4fRenderAt = c4fLauncherTimeMs();
    pthread_mutex_unlock(&c4fMutex);
    until([] { return view().user[0].state == C4F_TESTER_READY; });
    pthread_mutex_lock(&c4fMutex); c4fQuit = 1; pthread_mutex_unlock(&c4fMutex);
    pthread_join(worker, NULL);
    assert(largeMotor.load() == 0 && closes.load() >= 4);
    printf("PASS sampler: stalled display stops output; recovery and owned-handle cleanup are safe\n");

    C4fLauncherScreen a = {}, b;
    a.tester.active = a.tester.menu = 1; a.tester.selected = 0;
    b = a; b.controllers = 4; b.tester.l2 = 170; b.tester.gyro[2] = 230;
    C4fLauncherScreen va = c4fVisibleScreen(&a), vb = c4fVisibleScreen(&b);
    assert(!memcmp(&va, &vb, sizeof(va)));
    b.tester.focus = 1; vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    b = a; b.tester.rumble = 1; vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    a.connection.available = 1; a.connection.pads = 1U << 1;
    for (int p = 0; p < C4F_SERVICE_PADS; p++) a.connection.rttMs[p] = -1;
    a.connection.rttMs[1] = 8;
    b = a; b.connection.rttMs[1] = 9;
    va = c4fVisibleScreen(&a); vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    b = a; b.connection.rttMs[1] = -1;
    vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    printf("PASS tester redraw key: fresh and expired connection RTT changes remain visible in tools\n");
    a.tester.colorMenu = 1;
    va = c4fVisibleScreen(&a);
    b = a; b.tester.gyro[2] = 190;
    vb = c4fVisibleScreen(&b); assert(!memcmp(&va, &vb, sizeof(va)));
    b.tester.colorChannel = 1;
    vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    b = a; b.tester.colorFine = 1;
    vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    b = a; b.tester.light[0] = 128;
    vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    printf("PASS RGB picker redraw key: selection, precision and color visible; hidden live motion ignored\n");
    b = a;
    a.tester.menu = b.tester.menu = 0; b.tester.gyro[2] = 200;
    va = c4fVisibleScreen(&a); vb = c4fVisibleScreen(&b); assert(memcmp(&va, &vb, sizeof(va)));
    printf("PASS tester redraw key: hidden readings ignored by tools; live view and output stay visible\n");
}
