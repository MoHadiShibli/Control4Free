/* Native PS4 launcher. The controller service runs outside this application. */
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include <orbis/NetCtl.h>
#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include "autorun.h"
#include "input.h"
#include "navigation.h"
#include "sandbox.h"
#include "screen.h"
#include "service.h"
#ifdef C4F_DIAG
#include "diag.h"
#endif

#define C4F_BUNDLED_PAYLOAD "/app0/assets/control4free.elf"

/* Worker commands. */
enum { C4F_DO_NOTHING, C4F_DO_START, C4F_DO_STOP, C4F_DO_SET_UP, C4F_DO_AUTORUN_ON, C4F_DO_AUTORUN_OFF };

static pthread_mutex_t c4fMutex = PTHREAD_MUTEX_INITIALIZER;
static C4fLauncherScreen c4fScreen;
static int c4fCommand, c4fQuit;

/* Set up: auto-start on, then start it now if PayLoader takes it. */
static int c4fSetUp(char *message, size_t size)
{
    int result = c4fAutorunEnable(message, size);
    if (result) return result;
    char started[160];
    size_t payloadSize;
    const unsigned char *payload = c4fAutorunBundled(&payloadSize);
    result = c4fLauncherStart(payload, payloadSize, started, sizeof(started));
    if (result == 0) snprintf(message, size, "Set up and running. GoldHEN will also start it after each restart.");
    else if (result == -2) snprintf(message, size, "%s", started);
    else snprintf(message, size, "Auto-start is on: Control4Free starts the next time GoldHEN loads. %s", started);
    return result == -2 ? -2 : 0;
}

static void *c4fWorker(void *)
{
    uint64_t nextCheck = 0;
    int first = 1, autorun = C4F_AUTORUN_UNKNOWN;
    char autorunNote[96] = {0}, shownProblem[128] = {0};
    for (;;) {
        pthread_mutex_lock(&c4fMutex);
        int command = c4fCommand, quit = c4fQuit;
        c4fCommand = C4F_DO_NOTHING;
        pthread_mutex_unlock(&c4fMutex);
        if (quit) return NULL;
        if (command || c4fLauncherTimeMs() >= nextCheck) {
            char message[160] = {0}, address[64] = {0};
            [[maybe_unused]] int result = 0; /* read by the diagnostic build */
            /* The console's own address: shown for the phone or PC, and the
             * app's second way to reach Control4Free from its sandbox. */
            OrbisNetCtlInfo info;
            memset(&info, 0, sizeof(info));
            if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0 &&
                info.ip_address[0] && strcmp(info.ip_address, "0.0.0.0")) {
                snprintf(address, sizeof(address), "http://%.15s:4264", info.ip_address);
                c4fLauncherSetHost(info.ip_address);
            } else {
                c4fLauncherSetHost(NULL);
            }
#ifdef C4F_DIAG
            if (command == C4F_DO_START || command == C4F_DO_SET_UP) c4fDiagBeforeStart();
#endif
            if (command == C4F_DO_START) {
                size_t payloadSize;
                const unsigned char *payload = c4fAutorunBundled(&payloadSize);
                result = c4fLauncherStart(payload, payloadSize, message, sizeof(message));
            }
            if (command == C4F_DO_STOP) result = c4fLauncherStop(message, sizeof(message));
            if (command == C4F_DO_SET_UP) result = c4fSetUp(message, sizeof(message));
#ifdef C4F_DIAG
            if (command == C4F_DO_START || command == C4F_DO_SET_UP) c4fDiagAfterStart(result, message);
#endif
            if (command == C4F_DO_AUTORUN_ON) result = c4fAutorunEnable(message, sizeof(message));
            if (command == C4F_DO_AUTORUN_OFF) result = c4fAutorunDisable(message, sizeof(message));
            if (first || command >= C4F_DO_SET_UP) autorun = c4fAutorunCheck(autorunNote, sizeof(autorunNote));
            C4fServiceStatus status;
            int running = c4fLauncherProbe(&status);
            const char *problem = c4fLauncherProblem();
            pthread_mutex_lock(&c4fMutex);
            if (command) {
                snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s", message);
            } else if (!c4fScreen.locked &&
                       (first || running != c4fScreen.running || (running < 0 && strcmp(problem, shownProblem)))) {
                if (running == 1) snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Ready. Open the address on your phone or PC.");
                else if (running == 0) snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Not running.");
                else if (!c4fLauncherReached()) snprintf(c4fScreen.message, sizeof(c4fScreen.message), "The app cannot reach Control4Free: %s", problem);
                else snprintf(c4fScreen.message, sizeof(c4fScreen.message), "No answer the app understands: %s", problem);
                snprintf(shownProblem, sizeof(shownProblem), "%s", problem);
            }
            c4fScreen.running = running;
            c4fScreen.controllers = status.controllers;
            snprintf(c4fScreen.runningVersion, sizeof(c4fScreen.runningVersion), "%s", running == 1 ? status.version : "");
            c4fScreen.autorun = autorun;
            snprintf(c4fScreen.autorunNote, sizeof(c4fScreen.autorunNote), "%s", autorunNote);
            snprintf(c4fScreen.address, sizeof(c4fScreen.address), "%s", address);
            /* A queued click while a periodic probe was in flight remains busy. */
            if (!c4fCommand) c4fScreen.busy = 0;
#ifdef C4F_DIAG
            C4fLauncherScreen copy = c4fScreen;
            pthread_mutex_unlock(&c4fMutex);
            C4fDiagState diag = { running, status.controllers, autorun, copy.locked, copy.busy, copy.confirmStop,
                                  status.version, problem, copy.message, copy.address, copy.autorunNote, copy.inputNote };
            unsigned generation = c4fDiagRefresh(&diag);
            pthread_mutex_lock(&c4fMutex);
            c4fScreen.diagGeneration = generation;
#endif
            pthread_mutex_unlock(&c4fMutex);
            first = 0; nextCheck = c4fLauncherTimeMs() + 2000;
        }
        usleep(100000);
    }
}

static int c4fVideo = -1;
static OrbisKernelEqueue c4fFlipQueue;
static int c4fHasQueue, c4fBufferRegistration = -1;
static void *c4fVideoMemory;
static off_t c4fVideoOffset;
static size_t c4fVideoSize;
static void *c4fBuffers[2];

static int c4fInitVideo(void)
{
    c4fVideo = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (c4fVideo < 0) return -1;
    if (sceKernelCreateEqueue(&c4fFlipQueue, "Control4Free screen") < 0) return -1;
    c4fHasQueue = 1;
    if (sceVideoOutAddFlipEvent(c4fFlipQueue, c4fVideo, NULL) < 0) return -1;
    const size_t frameSize = C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT * sizeof(uint32_t);
    const size_t alignment = 0x200000;
    size_t size = (frameSize * 2 + alignment - 1) & ~(alignment - 1);
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, alignment, 3, &c4fVideoOffset) < 0) return -1;
    c4fVideoSize = size;
    if (sceKernelMapDirectMemory(&c4fVideoMemory, size, 0x33, 0, c4fVideoOffset, alignment) < 0) return -1;
    c4fBuffers[0] = c4fVideoMemory;
    c4fBuffers[1] = (char *)c4fVideoMemory + frameSize;
    OrbisVideoOutBufferAttribute attr;
    memset(&attr, 0, sizeof(attr));
    /* Same linear ARGB format as the OpenOrbis software-rendering sample. */
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT, C4F_SCREEN_WIDTH);
    c4fBufferRegistration = sceVideoOutRegisterBuffers(c4fVideo, 0, c4fBuffers, 2, &attr);
    if (c4fBufferRegistration < 0) return -1;
    return sceVideoOutSetFlipRate(c4fVideo, 1); /* 30 Hz is enough for a launcher. */
}

static void c4fCloseVideo(void)
{
    if (c4fVideo >= 0) {
        if (c4fBufferRegistration >= 0) sceVideoOutUnregisterBuffers(c4fVideo, c4fBufferRegistration);
        sceVideoOutClose(c4fVideo);
    }
    if (c4fHasQueue) sceKernelDeleteEqueue(c4fFlipQueue);
    if (c4fVideoMemory) sceKernelMunmap(c4fVideoMemory, c4fVideoSize);
    if (c4fVideoSize) sceKernelReleaseDirectMemory(c4fVideoOffset, c4fVideoSize);
}

/* The pinned SDK omits this error from Pad.h. A handle already opened by the
 * application can be borrowed, but must not be closed by our reader. */
#define C4F_PAD_ERROR_ALREADY_OPENED 0x80920004u
static_assert(C4F_INPUT_SYSTEM_USER == ORBIS_USER_SERVICE_USER_ID_SYSTEM, "system input user");
static_assert(C4F_INPUT_PORT_STANDARD == ORBIS_PAD_PORT_TYPE_STANDARD, "standard input port");
static_assert(C4F_INPUT_BUTTON_UP == ORBIS_PAD_BUTTON_UP, "Up button");
static_assert(C4F_INPUT_BUTTON_RIGHT == ORBIS_PAD_BUTTON_RIGHT, "Right button");
static_assert(C4F_INPUT_BUTTON_DOWN == ORBIS_PAD_BUTTON_DOWN, "Down button");
static_assert(C4F_INPUT_BUTTON_LEFT == ORBIS_PAD_BUTTON_LEFT, "Left button");
static_assert(C4F_INPUT_BUTTON_CROSS == ORBIS_PAD_BUTTON_CROSS, "OK button");
static_assert(C4F_INPUT_BUTTON_CIRCLE == ORBIS_PAD_BUTTON_CIRCLE, "Back button");
static_assert(C4F_INPUT_BUTTON_OPTIONS == ORBIS_PAD_BUTTON_OPTIONS, "Menu button");
static_assert(offsetof(OrbisPadData, unknown) + 3 == 108, "remote-control key byte");

enum { C4F_INPUT_WAITING, C4F_INPUT_READY, C4F_INPUT_DISCONNECTED,
       C4F_INPUT_OPEN_FAILED, C4F_INPUT_READ_FAILED, C4F_INPUT_USER_FAILED };

typedef struct {
    int initialized, handles[2], state[2];
    uint32_t error[2], buttons[2];
    uint32_t remoteLastRawButtons;
    uint8_t remoteLastKey;
} C4fNativeInput;

static const char *const c4fInputNames[] = { "DS4", "TV remote" };
static const char *const c4fInputStates[] = {
    "waiting", "ready", "disconnected", "open failed", "read failed", "user unavailable"
};

static void c4fNativeInputState(C4fNativeInput *input, int source, int state, int result)
{
    if (input->state[source] != state || input->error[source] != (uint32_t)result)
        printf("[c4f-launcher] %s input: %s (0x%08x)\n",
               c4fInputNames[source], c4fInputStates[state], (unsigned)result);
    input->state[source] = state;
    input->error[source] = (uint32_t)result;
    if (state != C4F_INPUT_READY) input->buttons[source] = 0;
}

static int c4fNativeInitialUser(void *context, int32_t *user)
{
    int result = sceUserServiceGetInitialUser(user);
    if (result) c4fNativeInputState((C4fNativeInput *)context, 0, C4F_INPUT_USER_FAILED, result);
    return result;
}

static int c4fNativeOpen(void *context, int32_t user, int port, int *owned)
{
    C4fNativeInput *input = (C4fNativeInput *)context;
    int source = port == ORBIS_PAD_PORT_TYPE_STANDARD ? 0 : 1;
    *owned = 0;
    if (!input->initialized) {
        int result = scePadInit();
        if (result) {
            c4fNativeInputState(input, source, C4F_INPUT_OPEN_FAILED, result);
            return -1;
        }
        input->initialized = 1;
    }
    int handle = scePadOpen(user, port, 0, NULL);
    if ((uint32_t)handle == C4F_PAD_ERROR_ALREADY_OPENED)
        handle = scePadGetHandle(user, port, 0);
    else if (handle >= 0)
        *owned = 1;
    input->handles[source] = handle;
    if (handle < 0) c4fNativeInputState(input, source, C4F_INPUT_OPEN_FAILED, handle);
    return handle;
}

static int c4fNativeRead(void *context, int handle, uint32_t *buttons, int *connected)
{
    C4fNativeInput *input = (C4fNativeInput *)context;
    int source = handle == input->handles[0] ? 0 : 1;
    OrbisPadData sample;
    memset(&sample, 0, sizeof(sample));
    int result = scePadReadState(handle, &sample);
    uint32_t mappedButtons = sample.buttons;
    if (source == C4F_INPUT_REMOTE && result == 0) {
        /* The SDK's byte108 is deviceUniqueData[0] in the reverse PS4 layout.
         * PS5 implementations use it for remote Enter/Back/Menu even when the
         * device-data length is zero. PS4 key decoding still needs hardware
         * confirmation. */
        uint8_t keyCode = sample.unknown[3];
        if (keyCode || sample.buttons) {
            input->remoteLastKey = keyCode;
            input->remoteLastRawButtons = sample.buttons;
        }
        mappedButtons = c4fInputRemoteButtons(sample.buttons, keyCode);
    }
    *buttons = result == 0 && sample.connected ? mappedButtons : 0;
    *connected = result == 0 && sample.connected;
    c4fNativeInputState(input, source, result ? C4F_INPUT_READ_FAILED :
                       sample.connected ? C4F_INPUT_READY : C4F_INPUT_DISCONNECTED, result);
    input->buttons[source] = *buttons;
    return result;
}

static void c4fNativeClose(void *, int handle) { scePadClose(handle); }

static void c4fNativeInputNote(const C4fNativeInput *input, char *note, size_t size)
{
    snprintf(note, size, "%s: %s (0x%08x); %s: %s (0x%08x), last key 0x%02x / raw 0x%08x",
             c4fInputNames[0], c4fInputStates[input->state[0]],
             input->error[0] ? input->error[0] : input->buttons[0],
             c4fInputNames[1], c4fInputStates[input->state[1]],
             input->error[1] ? input->error[1] : input->buttons[1],
             (unsigned)input->remoteLastKey, input->remoteLastRawButtons);
}

int main(void)
{
    const size_t frameBytes = (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT * sizeof(uint32_t);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[c4f-launcher] start " C4F_LAUNCHER_VERSION "\n");
    /* A peer closing mid-send must be an error, not the end of the app. */
    signal(SIGPIPE, SIG_IGN);
    /* Frames are composed in cached memory: blending reads pixels back, which
     * is very slow from the write-combined framebuffers. */
    uint32_t *canvas = (uint32_t *)malloc(frameBytes);
    if (!canvas || c4fScreenInit() < 0 || c4fInitVideo() < 0) {
        printf("[c4f-launcher] display initialization failed\n");
        c4fCloseVideo(); free(canvas); return 1;
    }
    sceNetCtlInit();
    OrbisUserServiceInitializeParams params = {};
    params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&params);
    C4fNativeInput nativeInput = {};
    nativeInput.handles[0] = nativeInput.handles[1] = -1;
    C4fInputOps inputOps = { &nativeInput, c4fNativeInitialUser, c4fNativeOpen, c4fNativeRead, c4fNativeClose };
    C4fInput inputs;
    c4fInputInit(&inputs, &inputOps);
    c4fScreen.running = -1; c4fScreen.busy = 1; c4fScreen.autorun = C4F_AUTORUN_UNKNOWN;
    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Checking Control4Free...");
    /* Read before leaving the sandbox: /app0 is only visible from inside it. */
    if (c4fAutorunLoadBundled(C4F_BUNDLED_PAYLOAD)) printf("[c4f-launcher] bundled payload unreadable\n");
    /* The sandbox refuses connections to the console itself (EACCES), so the
     * app could reach neither Control4Free nor PayLoader from inside it. */
    int sandbox = c4fSandboxLeave(), sandboxError = errno;
    if (sandbox) printf("[c4f-launcher] could not leave the sandbox, errno %d\n", sandboxError);
#ifdef C4F_DIAG
    c4fDiagStart(sandbox, sandboxError);
    for (int i = 0; i < C4F_DIAG_PAGES; i++) c4fScreen.diagScroll[i] = i ? C4F_DIAG_END : 0;
#endif
    pthread_t worker;
    int workerStarted = pthread_create(&worker, NULL, c4fWorker, NULL) == 0;
    if (!workerStarted) {
        pthread_mutex_lock(&c4fMutex);
        c4fScreen.locked = 1;
        snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Could not initialize launcher. Close with PS and retry.");
        pthread_mutex_unlock(&c4fMutex);
    }
    sceSystemServiceHideSplashScreen();
    uint64_t lastInputAt = c4fLauncherTimeMs();
    int buffer = 0, done = 0, drawn = 0, shown[2] = { 0, 0 };
    int64_t frame = 1;
    C4fLauncherScreen drawnState;
    memset(&drawnState, 0, sizeof(drawnState));
    while (!done) {
        uint64_t now = c4fLauncherTimeMs();
        /* A suspended app must not resume a pending confirmation or reuse a
         * held navigation button. Do not guess unreversed focus API signatures. */
        int resumed = now > lastInputAt && now - lastInputAt > 1000;
        if (resumed) c4fInputReset(&inputs, now);
        lastInputAt = now;
        C4fInputFrame input;
        c4fInputPoll(&inputs, now, &input);
        pthread_mutex_lock(&c4fMutex);
        c4fNativeInputNote(&nativeInput, c4fScreen.inputNote, sizeof(c4fScreen.inputNote));
        if (resumed) {
            c4fScreen.actionMenu = c4fScreen.confirmStop = c4fScreen.confirmMenu = 0;
            c4fScreen.actionFocus = C4F_ACTION_NONE;
        }
        uint32_t standardUnhandled = 0, remoteUnhandled = 0;
#ifdef C4F_DIAG
        const int diagnostic = 1;
#else
        const int diagnostic = 0;
#endif
        C4fLauncherAction action = c4fNavigate(&c4fScreen, input.standard.pressed, input.remote.pressed,
                                              diagnostic, &standardUnhandled, &remoteUnhandled);
        if (action == C4F_ACTION_CLOSE) done = 1;
        else if (action == C4F_ACTION_START) {
            /* Until auto-start is on, Start sets it up and starts it. */
            int setUp = c4fScreen.autorun != C4F_AUTORUN_ON;
            c4fCommand = setUp ? C4F_DO_SET_UP : C4F_DO_START; c4fScreen.busy = 1;
            snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s",
                     setUp ? "Setting up Control4Free. Please wait..." : "Starting Control4Free. Please wait...");
        } else if (action == C4F_ACTION_STOP) {
            c4fCommand = C4F_DO_STOP; c4fScreen.busy = 1;
            snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Stopping and releasing controllers...");
        } else if (action == C4F_ACTION_AUTORUN) {
            int off = c4fScreen.autorun == C4F_AUTORUN_ON;
            c4fCommand = off ? C4F_DO_AUTORUN_OFF : C4F_DO_AUTORUN_ON; c4fScreen.busy = 1;
            snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s",
                     off ? "Turning auto-start off..." : "Turning auto-start on...");
        }
#ifdef C4F_DIAG
        /* Pages and scrolling work while busy too. */
        uint32_t pressed = standardUnhandled | remoteUnhandled;
        int page = c4fScreen.diagPage;
        if (pressed & (ORBIS_PAD_BUTTON_R1 | ORBIS_PAD_BUTTON_RIGHT)) c4fScreen.diagPage = (page + 1) % C4F_DIAG_PAGES;
        if (pressed & (ORBIS_PAD_BUTTON_L1 | ORBIS_PAD_BUTTON_LEFT)) c4fScreen.diagPage = (page + C4F_DIAG_PAGES - 1) % C4F_DIAG_PAGES;
        if (pressed & (ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN)) {
            int max = c4fDiagMaxScroll(page), at = c4fScreen.diagScroll[page];
            if (at >= C4F_DIAG_END) at = max;
            at += pressed & ORBIS_PAD_BUTTON_UP ? -10 : 10;
            c4fScreen.diagScroll[page] = at >= max ? C4F_DIAG_END : at < 0 ? 0 : at;
        }
        if (action == C4F_ACTION_CAPTURE || (pressed & ORBIS_PAD_BUTTON_OPTIONS)) c4fDiagCapture(4000);
#endif
        C4fLauncherScreen snapshot = c4fScreen;
        pthread_mutex_unlock(&c4fMutex);
        /* Redraw only when something changed, then copy it to each buffer once. */
        if (!drawn || memcmp(&snapshot, &drawnState, sizeof(snapshot))) {
#ifdef C4F_DIAG
            c4fDrawDiag(canvas, &snapshot);
#else
            c4fDrawLauncher(canvas, &snapshot);
#endif
            drawnState = snapshot; drawn = 1; shown[0] = shown[1] = 0;
        }
        if (!shown[buffer]) { memcpy(c4fBuffers[buffer], canvas, frameBytes); shown[buffer] = 1; }
        if (sceVideoOutSubmitFlip(c4fVideo, buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC, frame) < 0) break;
        for (;;) {
            OrbisVideoOutFlipStatus status;
            memset(&status, 0, sizeof(status));
            if (sceVideoOutGetFlipStatus(c4fVideo, &status) < 0) { done = 1; break; }
            if (status.flipArg >= frame) break;
            OrbisKernelEvent event;
            int count;
            OrbisKernelUseconds timeout = 100000;
            sceKernelWaitEqueue(c4fFlipQueue, &event, 1, &count, &timeout);
        }
        buffer ^= 1; frame++;
    }
    pthread_mutex_lock(&c4fMutex); c4fQuit = 1; pthread_mutex_unlock(&c4fMutex);
    if (workerStarted) pthread_join(worker, NULL);
#ifdef C4F_DIAG
    c4fDiagStop();
#endif
    c4fInputClose(&inputs);
    sceNetCtlTerm();
    c4fCloseVideo();
    free(canvas);
    /* Closing this app intentionally does not stop the external payload. */
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
