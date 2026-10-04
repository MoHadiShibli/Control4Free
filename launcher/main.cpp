/* Native PS4 launcher. The controller service runs outside this application. */
#include <errno.h>
#include <signal.h>
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
#include "sandbox.h"
#include "screen.h"
#include "service.h"

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
            int result = 0;
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
            if (command == C4F_DO_START) {
                size_t payloadSize;
                const unsigned char *payload = c4fAutorunBundled(&payloadSize);
                result = c4fLauncherStart(payload, payloadSize, message, sizeof(message));
            }
            if (command == C4F_DO_STOP) result = c4fLauncherStop(message, sizeof(message));
            if (command == C4F_DO_SET_UP) result = c4fSetUp(message, sizeof(message));
            if (command == C4F_DO_AUTORUN_ON) result = c4fAutorunEnable(message, sizeof(message));
            if (command == C4F_DO_AUTORUN_OFF) result = c4fAutorunDisable(message, sizeof(message));
            if (first || command >= C4F_DO_SET_UP) autorun = c4fAutorunCheck(autorunNote, sizeof(autorunNote));
            C4fServiceStatus status;
            int running = c4fLauncherProbe(&status);
            const char *problem = c4fLauncherProblem();
            pthread_mutex_lock(&c4fMutex);
            if (result == -2) c4fScreen.locked = 1;
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
            c4fScreen.autorun = autorun;
            snprintf(c4fScreen.autorunNote, sizeof(c4fScreen.autorunNote), "%s", autorunNote);
            snprintf(c4fScreen.address, sizeof(c4fScreen.address), "%s", address);
            /* A queued click while a periodic probe was in flight remains busy. */
            if (!c4fCommand) c4fScreen.busy = 0;
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
    int32_t user = -1, pad = -1;
    if (scePadInit() == 0 && sceUserServiceGetInitialUser(&user) == 0)
        pad = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    c4fScreen.running = -1; c4fScreen.busy = 1; c4fScreen.autorun = C4F_AUTORUN_UNKNOWN;
    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Checking Control4Free...");
    /* Read before leaving the sandbox: /app0 is only visible from inside it. */
    if (c4fAutorunLoadBundled(C4F_BUNDLED_PAYLOAD)) printf("[c4f-launcher] bundled payload unreadable\n");
    /* The sandbox refuses connections to the console itself (EACCES), so the
     * app could reach neither Control4Free nor PayLoader from inside it. */
    if (c4fSandboxLeave()) printf("[c4f-launcher] could not leave the sandbox, errno %d\n", errno);
    pthread_t worker;
    int workerStarted = pthread_create(&worker, NULL, c4fWorker, NULL) == 0;
    if (!workerStarted || pad < 0) {
        pthread_mutex_lock(&c4fMutex);
        c4fScreen.locked = 1;
        snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Could not initialize launcher. Close with PS and retry.");
        pthread_mutex_unlock(&c4fMutex);
    }
    sceSystemServiceHideSplashScreen();
    uint32_t previous = 0;
    int buffer = 0, done = 0, drawn = 0, shown[2] = { 0, 0 };
    int64_t frame = 1;
    C4fLauncherScreen drawnState;
    memset(&drawnState, 0, sizeof(drawnState));
    while (!done) {
        OrbisPadData input;
        memset(&input, 0, sizeof(input));
        uint32_t buttons = pad >= 0 && scePadReadState(pad, &input) == 0 && input.connected ? input.buttons : 0;
        uint32_t pressed = buttons & ~previous;
        previous = buttons;
        pthread_mutex_lock(&c4fMutex);
        if (!c4fScreen.busy) {
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                if (c4fScreen.confirmStop) c4fScreen.confirmStop = 0;
                else done = 1;
            } else if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                if (c4fScreen.confirmStop) {
                    c4fCommand = C4F_DO_STOP; c4fScreen.confirmStop = 0; c4fScreen.busy = 1;
                    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Stopping and releasing controllers...");
                } else if (!c4fScreen.locked && c4fScreen.running != 1) {
                    /* Until auto-start is on, Cross sets it up and starts it. */
                    int setUp = c4fScreen.autorun != C4F_AUTORUN_ON;
                    c4fCommand = setUp ? C4F_DO_SET_UP : C4F_DO_START; c4fScreen.busy = 1;
                    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s",
                             setUp ? "Setting up Control4Free. Please wait..." : "Starting Control4Free. Please wait...");
                }
            } else if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) && !c4fScreen.confirmStop &&
                       c4fScreen.autorun != C4F_AUTORUN_UNKNOWN) {
                int off = c4fScreen.autorun == C4F_AUTORUN_ON;
                c4fCommand = off ? C4F_DO_AUTORUN_OFF : C4F_DO_AUTORUN_ON; c4fScreen.busy = 1;
                snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s",
                         off ? "Turning auto-start off..." : "Turning auto-start on...");
            } else if ((pressed & ORBIS_PAD_BUTTON_SQUARE) && c4fScreen.running == 1) c4fScreen.confirmStop = 1;
        }
        C4fLauncherScreen snapshot = c4fScreen;
        pthread_mutex_unlock(&c4fMutex);
        /* Redraw only when something changed, then copy it to each buffer once. */
        if (!drawn || memcmp(&snapshot, &drawnState, sizeof(snapshot))) {
            c4fDrawLauncher(canvas, &snapshot);
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
    if (pad >= 0) scePadClose(pad);
    sceNetCtlTerm();
    c4fCloseVideo();
    free(canvas);
    /* Closing this app intentionally does not stop the external payload. */
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
