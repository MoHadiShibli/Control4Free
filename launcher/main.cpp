/* Native PS4 launcher. The controller service runs outside this application. */
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
#include "screen.h"
#include "service.h"

static pthread_mutex_t c4fMutex = PTHREAD_MUTEX_INITIALIZER;
static C4fLauncherScreen c4fScreen;
static int c4fCommand, c4fQuit;

static void *c4fWorker(void *)
{
    uint64_t nextCheck = 0;
    int first = 1;
    for (;;) {
        pthread_mutex_lock(&c4fMutex);
        int command = c4fCommand, quit = c4fQuit;
        c4fCommand = 0;
        pthread_mutex_unlock(&c4fMutex);
        if (quit) return NULL;
        if (command || c4fLauncherTimeMs() >= nextCheck) {
            char message[160] = {0}, address[64] = {0};
            int result = 0;
            if (command == 1) result = c4fLauncherStart("/app0/assets/control4free.elf", message, sizeof(message));
            if (command == 2) result = c4fLauncherStop(message, sizeof(message));
            C4fServiceStatus status;
            int running = c4fLauncherProbe(&status);
            OrbisNetCtlInfo info;
            memset(&info, 0, sizeof(info));
            if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0 &&
                info.ip_address[0] && strcmp(info.ip_address, "0.0.0.0"))
                snprintf(address, sizeof(address), "http://%.15s:4264", info.ip_address);
            pthread_mutex_lock(&c4fMutex);
            if (result == -2) c4fScreen.locked = 1;
            if (command) snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s", message);
            else if (!c4fScreen.locked && (first || running != c4fScreen.running)) {
                snprintf(c4fScreen.message, sizeof(c4fScreen.message), "%s",
                    running == 1 ? "Ready. Scan the code or open the address on your phone." :
                    running == 0 ? "Not running. Press Cross to start it." :
                    "Something else answers on port 4264. Stop the old copy from its phone page.");
            }
            c4fScreen.running = running;
            c4fScreen.controllers = status.controllers;
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
    c4fScreen.running = -1; c4fScreen.busy = 1;
    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Checking Control4Free...");
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
                    c4fCommand = 2; c4fScreen.confirmStop = 0; c4fScreen.busy = 1;
                    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Stopping and releasing controllers...");
                } else if (!c4fScreen.locked && c4fScreen.running != 1) {
                    c4fCommand = 1; c4fScreen.busy = 1;
                    snprintf(c4fScreen.message, sizeof(c4fScreen.message), "Starting Control4Free. Please wait...");
                }
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
