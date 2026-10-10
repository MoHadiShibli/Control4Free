#ifndef C4F_LAUNCHER_INPUT_H
#define C4F_LAUNCHER_INPUT_H
#include <stdint.h>

/* These sources use the native pad API independently. Remote-control input
 * belongs to the system user, rather than the launcher's signed-in user. */
enum { C4F_INPUT_STANDARD, C4F_INPUT_REMOTE, C4F_INPUT_SOURCE_COUNT };
enum { C4F_INPUT_PORT_STANDARD = 0, C4F_INPUT_PORT_REMOTE = 16 };
#define C4F_INPUT_SYSTEM_USER 0xff
#define C4F_INPUT_RETRY_MS 1000

/* Native read-side buttons used by launcher navigation. */
#define C4F_INPUT_BUTTON_OPTIONS  UINT32_C(0x000008)
#define C4F_INPUT_BUTTON_UP       UINT32_C(0x000010)
#define C4F_INPUT_BUTTON_RIGHT    UINT32_C(0x000020)
#define C4F_INPUT_BUTTON_DOWN     UINT32_C(0x000040)
#define C4F_INPUT_BUTTON_LEFT     UINT32_C(0x000080)
#define C4F_INPUT_BUTTON_L1       UINT32_C(0x000400)
#define C4F_INPUT_BUTTON_R1       UINT32_C(0x000800)
#define C4F_INPUT_BUTTON_TRIANGLE UINT32_C(0x001000)
#define C4F_INPUT_BUTTON_CIRCLE   UINT32_C(0x002000)
#define C4F_INPUT_BUTTON_CROSS    UINT32_C(0x004000)
#define C4F_INPUT_BUTTON_SQUARE   UINT32_C(0x008000)
/* PS4 reverse definitions identify bit31 as system-intercepted input. This is
 * a read-side flag, not an additional virtual-controller button. */
#define C4F_INPUT_BUTTON_INTERCEPTED UINT32_C(0x80000000)

typedef struct {
    void *context;
    int (*initialUser)(void *context, int32_t *user);
    /* Returns a handle, or a negative error. owned is true only for handles
     * opened here; an already-open handle may be borrowed and must not close. */
    int (*open)(void *context, int32_t user, int port, int *owned);
    int (*read)(void *context, int handle, uint32_t *buttons, int *connected);
    void (*close)(void *context, int handle);
} C4fInputOps;

typedef struct {
    int handle, owned, ready;
    int32_t user;
    uint32_t previous;
    uint64_t retryAt;
} C4fInputSource;

typedef struct {
    C4fInputOps ops;
    C4fInputSource sources[C4F_INPUT_SOURCE_COUNT];
} C4fInput;

typedef struct {
    uint32_t buttons, pressed;
    int available;
} C4fInputSample;

typedef struct {
    C4fInputSample standard, remote;
} C4fInputFrame;

#ifdef __cplusplus
extern "C" {
#endif
void c4fInputInit(C4fInput *input, const C4fInputOps *ops);
/* Adds navigation keys reported separately by the system remote-control
 * stream, preserving the ordinary button mask and unknown key codes. */
uint32_t c4fInputRemoteButtons(uint32_t rawButtons, uint8_t keyCode);
/* Each recovered source requires a neutral sample before generating edges.
 * Failure or disconnect produces neutral input and a bounded retry. */
void c4fInputPoll(C4fInput *input, uint64_t now, C4fInputFrame *frame);
/* Focus changes may reset sources; durable launcher state is unaffected. */
void c4fInputReset(C4fInput *input, uint64_t now);
void c4fInputClose(C4fInput *input);
#ifdef __cplusplus
}
#endif
#endif
