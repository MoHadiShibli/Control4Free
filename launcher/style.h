#ifndef C4F_LAUNCHER_STYLE_H
#define C4F_LAUNCHER_STYLE_H
/* The controller page's look, shared by the launcher's screens (screen.c,
 * tester_draw.c): palette, sizes and the drawn pieces. */
#include "draw.h"

/* Page CSS pixels to TV pixels. */
#define C4F_S 1.5f

/* The page's palette (:root). */
#define C4F_TEXT         C4F_RGBA(244, 247, 255, 1)
#define C4F_MUTED        C4F_RGBA(226, 236, 255, .72f)
#define C4F_FAINT        C4F_RGBA(226, 236, 255, .46f)
#define C4F_LINE         C4F_RGBA(255, 255, 255, .16f)
#define C4F_LINE_SOFT    C4F_RGBA(255, 255, 255, .09f)
#define C4F_PANEL        C4F_RGBA(3, 16, 44, .55f)
#define C4F_PANEL_STRONG C4F_RGBA(2, 11, 31, .86f)
#define C4F_TILE         C4F_RGBA(12, 40, 98, .5f)
#define C4F_ACCENT       C4F_RGBA(58, 140, 255, 1)
#define C4F_OK           C4F_RGBA(69, 224, 138, 1)
#define C4F_WARN         C4F_RGBA(255, 208, 77, 1)
#define C4F_BAD          C4F_RGBA(255, 107, 125, 1)
#define C4F_CIRCLE_KEY   C4F_RGBA(255, 102, 128, 1)
#define C4F_CROSS_KEY    C4F_RGBA(134, 182, 255, 1)
#define C4F_SQUARE_KEY   C4F_RGBA(255, 146, 220, 1)
#define C4F_TRIANGLE_KEY C4F_RGBA(63, 224, 184, 1)
#define C4F_WHITE        C4F_RGBA(255, 255, 255, 1)

/* Button guide in the PS4's manner, with the page's face-button colours. */
enum { C4F_KEY_CROSS, C4F_KEY_CIRCLE, C4F_KEY_SQUARE, C4F_KEY_TRIANGLE, C4F_KEY_OPTIONS };
typedef struct { int key; const char *label; } C4fHint;

void c4fPaintBackdrop(C4fCanvas *c, float scale);
/* The full-screen backdrop without the launcher's blurs, handed over once
 * (the caller frees it); NULL when it is gone or memory was short. */
uint32_t *c4fTakeCleanBackdrop(void);
/* backdrop-filter: blur(sigma) under a rounded box, done once on a still backdrop. */
void c4fBlurUnder(C4fCanvas *c, float x, float y, float w, float h, float r, float sigma);
/* The DualShock 4 mark, its cut-outs showing what is behind. */
void c4fMark(C4fCanvas *c, float x, float y, float width, C4fColor color);
/* A DualShock 4 with its light bar lit. */
void c4fLitController(C4fCanvas *c, float x, float y, float width, float angle,
                      C4fColor body, C4fColor light, int shadow);
void c4fTextCentered(C4fCanvas *c, int font, float size, float cx, float y, C4fColor color, const char *text);
/* .tile, .panel, .dot */
void c4fTile(C4fCanvas *c, float x, float y, float w, float h, C4fColor bar);
void c4fPanel(C4fCanvas *c, float x, float y, float w, float h, C4fColor fill, C4fColor line);
void c4fDot(C4fCanvas *c, float cx, float cy, C4fColor color, int glow);
/* Right-aligned at `right`, centred on cy. */
void c4fHints(C4fCanvas *c, float right, float cy, const C4fHint *hints, int count);

#endif
