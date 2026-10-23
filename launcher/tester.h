#ifndef C4F_LAUNCHER_TESTER_H
#define C4F_LAUNCHER_TESTER_H
/* Controller test: what each signed-in user's controller sends, read the way a
 * game reads it, with rumble and light-bar test actions. The native pad and
 * user calls are injected, so this logic also runs in host tests. */
#include <stdint.h>

#define C4F_TESTER_USERS 4
#define C4F_TESTER_HOLD_MS 2000
#define C4F_TESTER_RUMBLE_MS 1000
#define C4F_TESTER_RETRY_MS 1000
#define C4F_TESTER_NAME 72 /* Room for a complete 16-character UTF-8 username. */
#define C4F_TESTER_MENU_COLUMNS 4
#define C4F_TESTER_REPEAT_DELAY_MS 350
#define C4F_TESTER_REPEAT_MS 100

/* One sample as the read side reports it. */
typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, touches;
    struct { uint16_t x, y; uint8_t id; } touch[2];
    float quat[4];  /* orientation x y z w */
    float accel[3]; /* G */
    float gyro[3];  /* rad/s */
    int connected;
} C4fTesterSample;

typedef struct {
    void *context;
    /* Fills up to C4F_TESTER_USERS signed-in user IDs; returns how many, or a
     * negative error. */
    int (*loginUsers)(void *context, int32_t *users);
    /* The name the PS4 shows. Only ever drawn, never logged. */
    void (*userName)(void *context, int32_t user, char *name, int size);
    /* A handle, or a negative error. owned as in C4fInputOps: a handle borrowed
     * from the launcher's own reader must not be closed here. */
    int (*open)(void *context, int32_t user, int *owned);
    int (*read)(void *context, int handle, C4fTesterSample *sample);
    void (*close)(void *context, int handle);
    int (*vibrate)(void *context, int handle, uint8_t large, uint8_t small);
    int (*lightBar)(void *context, int handle, uint8_t r, uint8_t g, uint8_t b);
    int (*resetLightBar)(void *context, int handle);
    int (*resetOrientation)(void *context, int handle);
    int (*motionSensor)(void *context, int handle, int on);
    /* Optional: a reader changed state (for the log; slot, never the user). */
    void (*state)(void *context, int slot, int state, uint32_t error);
} C4fTesterOps;

/* Per user. */
enum { C4F_TESTER_OPENING, C4F_TESTER_READY, C4F_TESTER_NO_CONTROLLER, C4F_TESTER_FAILED, C4F_TESTER_SIGNED_OUT };

/* The menu, in order. */
typedef enum {
    C4F_TESTER_RESUME, C4F_TESTER_USER, C4F_TESTER_ORIENTATION, C4F_TESTER_LIGHT_COLOR,
    C4F_TESTER_RUMBLE_LARGE, C4F_TESTER_RUMBLE_SMALL, C4F_TESTER_LIGHT_RESET,
    C4F_TESTER_EXIT, C4F_TESTER_ITEMS
} C4fTesterItem;
#define C4F_TESTER_MENU_ROWS ((C4F_TESTER_ITEMS + C4F_TESTER_MENU_COLUMNS - 1) / C4F_TESTER_MENU_COLUMNS)

/* L3 and R3 held together for C4F_TESTER_HOLD_MS. */
typedef struct {
    int holding, fired;
    uint64_t since;
} C4fTesterHold;

typedef struct {
    int32_t user; /* -1: empty slot */
    int handle, owned, state, lightSet, seen, nameGenerated;
    uint32_t error, previous, pressed, menuBlocked, repeatDirection;
    uint64_t retryAt, repeatAt;
    C4fTesterHold hold;
    uint8_t light[3];
    char name[C4F_TESTER_NAME];
    C4fTesterSample sample;
} C4fTesterReader;

typedef struct {
    C4fTesterOps ops;
    int active, menu, focus, armed, selected, rumble, rumbleReader;
    int colorMenu, colorChannel, colorFine;
    unsigned menuOpeners;
    uint64_t usersAt, rumbleUntil;
    C4fTesterReader readers[C4F_TESTER_USERS];
    char note[96];
} C4fTester;

/* What the screen shows, in whole display units so that an unchanged reading
 * compares equal and causes no redraw. */
typedef struct {
    int active, menu, focus, selected, rumble, lightSet;
    int colorMenu, colorChannel, colorFine;
    uint8_t light[3];
    struct { int32_t id; char name[C4F_TESTER_NAME]; int state, used, nameGenerated; uint32_t error; } user[C4F_TESTER_USERS];
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, touches;
    uint16_t touchX[2], touchY[2];
    int16_t quat[4];  /* x 1000 */
    int16_t accel[3]; /* x 100, G */
    int16_t gyro[3];  /* x 100, rad/s */
    char note[96];
} C4fTesterView;

#ifdef __cplusplus
extern "C" {
#endif
/* True once per hold; the buttons must be released before it fires again. */
int c4fTesterHoldUpdate(C4fTesterHold *hold, uint32_t buttons, uint64_t now);
void c4fTesterInit(C4fTester *tester, const C4fTesterOps *ops);
void c4fTesterOpen(C4fTester *tester, uint64_t now);
/* One frame. remotePressed is the TV remote's new presses: OK and Options open
 * the menu, Back closes it or leaves. Returns 0 once the tester has closed. */
int c4fTesterFrame(C4fTester *tester, uint64_t now, uint32_t remotePressed);
/* The app lost focus: stop test output and reopen every reader afterwards. */
void c4fTesterSuspend(C4fTester *tester, uint64_t now);
/* Stops rumble, resets light bars this test set, closes owned handles. */
void c4fTesterClose(C4fTester *tester);
void c4fTesterView(const C4fTester *tester, C4fTesterView *view);
const char *c4fTesterItemLabel(int item);
/* Shared by menu drawing and activation. Unavailable tiles may take focus. */
int c4fTesterItemAvailable(const C4fTesterView *view, int item);
#ifdef __cplusplus
}
#endif
#endif
