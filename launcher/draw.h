#ifndef C4F_LAUNCHER_DRAW_H
#define C4F_LAUNCHER_DRAW_H
/* Software drawing into a cached ARGB buffer: anti-aliased shapes and text.
 * Blending reads the destination, so never draw straight into the
 * write-combined framebuffer. */
#include <stdint.h>

typedef struct { uint32_t *px; int w, h; } C4fCanvas;
typedef struct { uint8_t r, g, b, a; } C4fColor;
#define C4F_RGBA(r, g, b, a) ((C4fColor){ (r), (g), (b), (uint8_t)((a) * 255.0f + 0.5f) })

enum { C4F_FONT_LIGHT, C4F_FONT_REGULAR, C4F_FONT_COUNT };

int c4fDrawInit(void);

/* Signed distance to a rounded box: centre, half size, corner radius. */
float c4fRoundBoxDistance(float px, float py, float cx, float cy, float hx, float hy, float r);

void c4fBlend(C4fCanvas *c, int x, int y, C4fColor color, float coverage);
void c4fFillRect(C4fCanvas *c, int x, int y, int w, int h, C4fColor color);

/* Rounded rectangles; x/y/w/h/r in pixels, fractions allowed. */
void c4fFillRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, C4fColor fill);
/* A CSS "inset 0 0 0 <width>px" line along the inside edge. */
void c4fInsetRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float width, C4fColor line);
/* A linear gradient clipped to the shape: alpha a0 at the start, a1 from
 * `stop` on, along a CSS angle in degrees. */
void c4fSheenRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float angle,
                       C4fColor color, float a0, float a1, float stop);
/* The part of the shape between top and top+height (a tile's colour bar). */
void c4fBandRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float height, C4fColor color);
/* A CSS outer box-shadow: drawn only outside the shape itself. */
void c4fShadowRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r,
                        float dx, float dy, float blur, float spread, C4fColor color);

void c4fFillCircle(C4fCanvas *c, float cx, float cy, float r, C4fColor fill);
void c4fRing(C4fCanvas *c, float cx, float cy, float r, float width, C4fColor line);
/* Soft halo around a circle, like box-shadow 0 0 <blur>px <spread>px. */
void c4fGlowCircle(C4fCanvas *c, float cx, float cy, float r, float blur, float spread, C4fColor color);
/* A line with round caps. */
void c4fSegment(C4fCanvas *c, float x0, float y0, float x1, float y1, float width, C4fColor color);
void c4fSquareOutline(C4fCanvas *c, float cx, float cy, float half, float width, C4fColor color);
void c4fTriangleOutline(C4fCanvas *c, float cx, float cy, float radius, float width, C4fColor color);

/* Vector paths (moves, lines, cubic curves), filled with non-zero winding:
 * draw holes the opposite way round. Coordinates are in a design space that
 * a transform places on the canvas. */
typedef struct { char op; float v[6]; } C4fPathOp; /* 'M', 'L', 'C' (c1 c2 end) */
typedef struct { C4fPathOp *ops; int count, capacity; } C4fPath;
typedef struct { float originX, originY, scale, angle, x, y; } C4fPlacement; /* design origin -> canvas x,y */

void c4fPathMove(C4fPath *p, float x, float y);
void c4fPathLine(C4fPath *p, float x, float y);
void c4fPathCubic(C4fPath *p, float x1, float y1, float x2, float y2, float x, float y);
void c4fPathCircle(C4fPath *p, float cx, float cy, float r, int clockwise);
void c4fPathRoundRect(C4fPath *p, float x, float y, float w, float h, float r, int clockwise);
void c4fPathFree(C4fPath *p);
/* Coverage of the path at a placement, as an 8-bit mask the size of its
 * bounding box (caller frees *mask). 0 when nothing lands on the canvas. */
int c4fPathMask(const C4fCanvas *c, const C4fPath *p, C4fPlacement at, unsigned char **mask, int *x0, int *y0, int *w, int *h);
void c4fFillPath(C4fCanvas *c, const C4fPath *p, C4fPlacement at, C4fColor color);
/* The mask blurred and spread: a soft halo or drop shadow under a shape. */
void c4fShadowPath(C4fCanvas *c, const C4fPath *p, C4fPlacement at, float dx, float dy, int radius, C4fColor color);

/* Text on a baseline. Sizes are CSS font-size in pixels. ASCII only. */
float c4fText(C4fCanvas *c, int font, float size, float x, float y, C4fColor color, const char *text);
float c4fTextN(C4fCanvas *c, int font, float size, float x, float y, C4fColor color, const char *text, int length);
float c4fTextWidth(int font, float size, const char *text);
/* Word-wrapped text; returns the number of lines (drawn only if c is set). */
int c4fTextWrapped(C4fCanvas *c, int font, float size, float x, float y, float maxWidth,
                   float lineHeight, int maxLines, C4fColor color, const char *text);

#endif
