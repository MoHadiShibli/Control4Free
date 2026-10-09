#ifndef C4F_LAUNCHER_DIAG_H
#define C4F_LAUNCHER_DIAG_H
/* Diagnostic build (make -C launcher C4F_DIAG=1): plain pages of everything
 * that says why Control4Free does not start on a console, in place of the
 * normal screen, so a user can send screenshots instead of log files. */
#include <stdint.h>
#include "screen.h"
#ifdef __cplusplus
extern "C" {
#endif

#define C4F_DIAG_PAGES 5
/* A page's scroll position meaning "show the end". */
#define C4F_DIAG_END 1000000

typedef struct {
    int running, controllers, autorun, locked, busy, confirmStop;
    const char *version, *problem, *message, *address, *autorunNote, *inputNote;
} C4fDiagState;

/* Records what is known at start-up and reads the kernel log's backlog. */
void c4fDiagStart(int sandboxResult, int sandboxError);
void c4fDiagStop(void);
/* Holds the kernel log open for this long, reading it. */
void c4fDiagCapture(unsigned milliseconds);
/* Around a start: what the service log looked like before, and the result. */
void c4fDiagBeforeStart(void);
void c4fDiagAfterStart(int result, const char *message);
/* Rebuilds the pages; returns their generation, which changes with them. */
unsigned c4fDiagRefresh(const C4fDiagState *state);
void c4fDrawDiag(uint32_t *pixels, const C4fLauncherScreen *screen);
/* The largest useful scroll position of a page, as of its last drawing. */
int c4fDiagMaxScroll(int page);

#ifdef __cplusplus
}
#endif
#endif
