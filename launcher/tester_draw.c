/* The controller test screen, in the launcher's look, with the controller drawn
 * the way the controller page draws it (client/index.html: DEFAULT_LAYOUT and the
 * .pad-area styles). Static backgrounds and expensive control artwork are
 * cached. Live values still update every draw; tools have their own view. */
#include "screen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "style.h"

/* Layout, in TV pixels. */
#define C4F_T_LEFT     150.0f
#define C4F_T_RIGHT    1770.0f
#define C4F_T_TABS_Y   246.0f
#define C4F_T_TOP      320.0f
#define C4F_T_HEIGHT   600.0f
#define C4F_T_PAD_W    1000.0f
#define C4F_T_GAP      30.0f
#define C4F_T_INFO_X   (C4F_T_LEFT + C4F_T_PAD_W + C4F_T_GAP)
#define C4F_T_INFO_W   (C4F_T_RIGHT - C4F_T_INFO_X)
#define C4F_T_TEXT_X   (C4F_T_INFO_X + 36)
/* The controller picture: 2:1, like the page's mapping picture. */
#define C4F_P_X 166.0f
#define C4F_P_Y 336.0f
#define C4F_P_W 968.0f
#define C4F_P_H 484.0f

/* Right panel rows: baselines. */
#define C4F_T_ROW_STICKS   372.0f
#define C4F_T_ROW_TRIGGERS 496.0f
#define C4F_T_ROW_TOUCH    620.0f
#define C4F_T_ROW_MOTION   690.0f
#define C4F_T_ROW_LIGHT    862.0f
#define C4F_T_BAR_X        (C4F_T_TEXT_X + 70)
#define C4F_T_BAR_W        300.0f
#define C4F_T_COLUMN_X     (C4F_T_TEXT_X + 236)
#define C4F_T_COLUMN_W     70.0f

/* The page's controller colours. */
#define C4F_P_ENGRAVED C4F_RGBA(236, 240, 248, .55f)
#define C4F_P_PRINT    C4F_RGBA(236, 240, 248, .4f)
#define C4F_P_HIGHLIGHT C4F_RGBA(255, 255, 255, .16f)
#define C4F_P_EDGE     C4F_RGBA(255, 255, 255, .06f)

/* The page's DEFAULT_LAYOUT: centre in % of the picture, size in units of u. */
enum { C4F_C_L2, C4F_C_L1, C4F_C_R2, C4F_C_R1, C4F_C_SHARE, C4F_C_OPTIONS, C4F_C_TOUCHPAD, C4F_C_DPAD,
       C4F_C_FACE, C4F_C_PS, C4F_C_LSTICK, C4F_C_RSTICK, C4F_C_L3, C4F_C_R3, C4F_C_COUNT };
static const float c4fLayout[C4F_C_COUNT][4] = {
    { 14, 10, 22, 11 }, { 14, 23, 22, 8 }, { 86, 10, 22, 11 }, { 86, 23, 22, 8 },
    { 28.5f, 13, 10, 14 }, { 71.5f, 13, 10, 14 }, { 50, 35, 76, 48 },
    { 15, 55, 40, 40 }, { 85, 55, 40, 40 }, { 50, 75, 10, 10 },
    { 33, 79, 34, 34 }, { 67, 79, 34, 34 }, { 17, 88, 11, 11 }, { 83, 88, 11, 11 },
};
/* The page's stick travel: the knob moves this share of the stick's width. */
#define C4F_KNOB_TRAVEL 0.27f

typedef struct { float x, y, w, h; } C4fBox;
typedef struct { float at; C4fColor color; } C4fStop;

static const C4fPlacement c4fIdentity = { 0, 0, 1, 0, 0, 0 };
static uint32_t *c4fLayer;
static uint32_t *c4fToolsBase, *c4fToolsFrame;
static int c4fToolsBaseReady;

typedef struct { uint32_t *px; int x, y, w, h; } C4fSprite;
static C4fSprite c4fKnobs[3], c4fPressed[16], c4fTouchGlow, c4fLightStrip;
static uint32_t c4fSpriteAccent;
static int c4fHasSpriteAccent;
static uint32_t *c4fSpriteScratch;
typedef struct { unsigned char *px; int x, y, w, h; } C4fMask;
static C4fMask c4fTriggerMasks[2];

/* The page's size unit, --u: min(1cqh, .46cqw) of the picture. */
static float c4fUnit(void) { return fminf(C4F_P_H / 100, .46f * C4F_P_W / 100); }

static C4fBox c4fControl(int id)
{
    const float u = c4fUnit(), *l = c4fLayout[id];
    C4fBox b = { C4F_P_X + l[0] / 100 * C4F_P_W, C4F_P_Y + l[1] / 100 * C4F_P_H, l[2] * u, l[3] * u };
    b.x -= b.w / 2;
    b.y -= b.h / 2;
    return b;
}

static C4fColor c4fMix(C4fColor a, C4fColor b, float share)
{
    return (C4fColor){ (uint8_t)(a.r * share + b.r * (1 - share) + .5f), (uint8_t)(a.g * share + b.g * (1 - share) + .5f),
                       (uint8_t)(a.b * share + b.b * (1 - share) + .5f), 255 };
}

static C4fColor c4fAlpha(C4fColor c, float alpha)
{
    c.a = (uint8_t)(alpha * 255 + .5f);
    return c;
}

static C4fColor c4fHex(uint32_t rgb) { return (C4fColor){ (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, 255 }; }

static C4fColor c4fStopsAt(const C4fStop *s, int n, float t)
{
    if (t <= s[0].at) return s[0].color;
    for (int i = 1; i < n; i++) {
        if (t > s[i].at) continue;
        float f = (t - s[i - 1].at) / (s[i].at - s[i - 1].at);
        const C4fColor a = s[i - 1].color, b = s[i].color;
        return (C4fColor){ (uint8_t)(a.r + (b.r - a.r) * f + .5f), (uint8_t)(a.g + (b.g - a.g) * f + .5f),
                           (uint8_t)(a.b + (b.b - a.b) * f + .5f), (uint8_t)(a.a + (b.a - a.a) * f + .5f) };
    }
    return s[n - 1].color;
}

/* ---- shapes ---- */

/* A box with CSS border-radius corners, as horizontal/vertical radius pairs for
 * top-left, top-right, bottom-right and bottom-left, scaled down together the
 * way CSS does when they do not fit; inset shrinks it for an inner edge. */
static void c4fRoundedPath(C4fPath *p, C4fBox b, const float radii[8], float inset)
{
    const float k = .5523f;
    float r[8], fit = 1;
    const float sums[4][2] = { { b.w, radii[0] + radii[2] }, { b.w, radii[6] + radii[4] },
                               { b.h, radii[1] + radii[7] }, { b.h, radii[3] + radii[5] } };
    for (int i = 0; i < 4; i++) if (sums[i][1] > 0) fit = fminf(fit, sums[i][0] / sums[i][1]);
    for (int i = 0; i < 8; i++) r[i] = fmaxf(0, radii[i] * fit - inset);
    const float x = b.x + inset, y = b.y + inset, w = b.w - 2 * inset, h = b.h - 2 * inset;
    c4fPathMove(p, x + r[0], y);
    c4fPathLine(p, x + w - r[2], y);
    c4fPathCubic(p, x + w - r[2] + r[2] * k, y, x + w, y + r[3] - r[3] * k, x + w, y + r[3]);
    c4fPathLine(p, x + w, y + h - r[5]);
    c4fPathCubic(p, x + w, y + h - r[5] + r[5] * k, x + w - r[4] + r[4] * k, y + h, x + w - r[4], y + h);
    c4fPathLine(p, x + r[6], y + h);
    c4fPathCubic(p, x + r[6] - r[6] * k, y + h, x, y + h - r[7] + r[7] * k, x, y + h - r[7]);
    c4fPathLine(p, x, y + r[1]);
    c4fPathCubic(p, x, y + r[1] - r[1] * k, x + r[0] - r[0] * k, y, x + r[0], y);
}

/* A shape's radii, from CSS percentages of its box. */
static void c4fPercentRadii(float out[8], C4fBox b, float tl, float tr, float br, float bl,
                            float tlv, float trv, float brv, float blv)
{
    const float r[8] = { tl * b.w, tlv * b.h, tr * b.w, trv * b.h, br * b.w, brv * b.h, bl * b.w, blv * b.h };
    memcpy(out, r, sizeof(r));
}

static void c4fShapePath(C4fPath *p, int id, float inset)
{
    const C4fBox b = c4fControl(id);
    const float u = c4fUnit();
    float r[8];
    if (id == C4F_C_L2 || id == C4F_C_R2)
        c4fPercentRadii(r, b, .44f, .44f, .18f, .18f, .64f, .64f, .24f, .24f);
    else if (id == C4F_C_L1 || id == C4F_C_R1)
        c4fPercentRadii(r, b, .34f, .34f, .26f, .26f, .70f, .70f, .50f, .50f);
    else if (id == C4F_C_TOUCHPAD) {
        const float t = 3 * u, m = 7 * u, radii[8] = { t, t, t, t, m, m, m, m };
        memcpy(r, radii, sizeof(r));
    } else {
        for (int i = 0; i < 8; i++) r[i] = b.w;
    }
    c4fRoundedPath(p, b, r, inset);
}

/* Share and Options: a capsule under the printed name. */
static C4fBox c4fCapsule(int id)
{
    const float u = c4fUnit(), label = fmaxf(8, 1.9f * u), gap = 1.1f * u, w = 4.2f * u, h = 8 * u;
    const C4fBox b = c4fControl(id);
    const float top = b.y + (b.h - (label + gap + h)) / 2 + label + gap;
    return (C4fBox){ b.x + (b.w - w) / 2, top, w, h };
}

static void c4fCapsulePath(C4fPath *p, int id, float inset)
{
    const C4fBox b = c4fCapsule(id);
    float r[8];
    for (int i = 0; i < 8; i++) r[i] = b.w / 2;
    c4fRoundedPath(p, b, r, inset);
}

/* ---- painting through a shape ---- */

/* Fills a path with a gradient: vertical over [top, top + height], or radial
 * from (cx, cy) out to `radius` when it is above 0. */
static void c4fGradientPath(C4fCanvas *c, const C4fPath *p, const C4fStop *s, int n,
                            float top, float height, float cx, float cy, float radius)
{
    unsigned char *mask;
    int x0, y0, w, h;
    if (!c4fPathMask(c, p, c4fIdentity, &mask, &x0, &y0, &w, &h)) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned m = mask[y * w + x];
            if (!m) continue;
            float px = x0 + x + .5f, py = y0 + y + .5f;
            float t = radius > 0 ? hypotf(px - cx, py - cy) / radius : (py - top) / height;
            c4fBlend(c, x0 + x, y0 + y, c4fStopsAt(s, n, t), m / 255.0f);
        }
    free(mask);
}

/* CSS inset shadows along a shape's own edge: a ring `width` pixels wide, or
 * only the top edge (inset 0 1px 0). `below` keeps the part under that line. */
static void c4fInsetPath(C4fCanvas *c, const C4fPath *p, int width, int topOnly, float below, C4fColor color)
{
    unsigned char *mask;
    int x0, y0, w, h;
    if (!c4fPathMask(c, p, c4fIdentity, &mask, &x0, &y0, &w, &h)) return;
#define C4F_AT(sx, sy) ((sx) >= 0 && (sy) >= 0 && (sx) < w && (sy) < h ? mask[(sy) * w + (sx)] : 0)
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int m = mask[y * w + x], inner = m;
            if (!m || y0 + y < below) continue;
            /* Well inside the shape: nothing to draw, and no need to look around. */
            if (m == 255 && C4F_AT(x, y - width) == 255 && (topOnly || (C4F_AT(x, y + width) == 255 &&
                C4F_AT(x - width, y) == 255 && C4F_AT(x + width, y) == 255 && C4F_AT(x - width, y - width) == 255 &&
                C4F_AT(x + width, y - width) == 255 && C4F_AT(x - width, y + width) == 255 &&
                C4F_AT(x + width, y + width) == 255))) continue;
            for (int dy = -width; dy <= width && inner; dy++)
                for (int dx = topOnly ? 0 : -width; dx <= (topOnly ? 0 : width) && inner; dx++) {
                    if (topOnly && dy >= 0) continue;
                    int sx = x + dx, sy = y + dy;
                    int s = sx >= 0 && sy >= 0 && sx < w && sy < h ? mask[sy * w + sx] : 0;
                    if (s < inner) inner = s;
                }
            if (m > inner) c4fBlend(c, x0 + x, y0 + y, color, (m - inner) / 255.0f);
        }
#undef C4F_AT
    free(mask);
}

/* A path's outline as a stroke of even width, joints without double darkening. */
static void c4fStrokePath(C4fCanvas *c, const C4fPath *p, float width, C4fColor color)
{
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < p->count; i++)
        for (int k = 0; k < (p->ops[i].op == 'C' ? 3 : 1); k++) {
            minX = fminf(minX, p->ops[i].v[k * 2]); maxX = fmaxf(maxX, p->ops[i].v[k * 2]);
            minY = fminf(minY, p->ops[i].v[k * 2 + 1]); maxY = fmaxf(maxY, p->ops[i].v[k * 2 + 1]);
        }
    const int x0 = (int)floorf(minX - width) - 1, y0 = (int)floorf(minY - width) - 1;
    const int w = (int)ceilf(maxX + width) + 2 - x0, h = (int)ceilf(maxY + width) + 2 - y0;
    if (w <= 0 || h <= 0 || w > c->w * 2 || h > c->h * 2) return;
    float *cover = calloc((size_t)w * (size_t)h, sizeof(float));
    if (!cover) return;
    float x = 0, y = 0;
    for (int i = 0; i < p->count; i++) {
        const C4fPathOp *o = &p->ops[i];
        if (o->op == 'M') { x = o->v[0]; y = o->v[1]; continue; }
        const int steps = o->op == 'C' ? 10 : 1;
        for (int s = 1; s <= steps; s++) {
            float t = (float)s / steps, mt = 1 - t, nx, ny;
            if (o->op == 'C') {
                nx = mt * mt * mt * x + 3 * mt * mt * t * o->v[0] + 3 * mt * t * t * o->v[2] + t * t * t * o->v[4];
                ny = mt * mt * mt * y + 3 * mt * mt * t * o->v[1] + 3 * mt * t * t * o->v[3] + t * t * t * o->v[5];
            } else {
                nx = o->v[0]; ny = o->v[1];
            }
            /* Distance from each pixel to the segment, into the coverage map. */
            const float ax = x, ay = y, dx = nx - ax, dy = ny - ay, len2 = dx * dx + dy * dy;
            const int sx0 = (int)floorf(fminf(ax, nx) - width) - x0, sx1 = (int)ceilf(fmaxf(ax, nx) + width) - x0;
            const int sy0 = (int)floorf(fminf(ay, ny) - width) - y0, sy1 = (int)ceilf(fmaxf(ay, ny) + width) - y0;
            for (int py = sy0 < 0 ? 0 : sy0; py <= sy1 && py < h; py++)
                for (int px = sx0 < 0 ? 0 : sx0; px <= sx1 && px < w; px++) {
                    float qx = x0 + px + .5f - ax, qy = y0 + py + .5f - ay;
                    float f = len2 > 0 ? fmaxf(0, fminf(1, (qx * dx + qy * dy) / len2)) : 0;
                    float d = hypotf(qx - f * dx, qy - f * dy);
                    float cov = fmaxf(0, fminf(1, width / 2 + .5f - d));
                    if (cov > cover[py * w + px]) cover[py * w + px] = cov;
                }
            x = nx; y = ny;
        }
    }
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++)
            if (cover[py * w + px] > 0) c4fBlend(c, x0 + px, y0 + py, color, cover[py * w + px]);
    free(cover);
}

/* Appends an SVG path's d (M L H V C S Q Z, either case) to p, a point (x, y)
 * landing at (ox + x * scale, oy + y * scale). Closing adds the line back to the
 * start, so the edges can be stroked as well as filled. */
static void c4fSvgPath(C4fPath *p, const char *d, float ox, float oy, float scale)
{
    float x = 0, y = 0, sx = 0, sy = 0, cx = 0, cy = 0, v[6];
    char command = 0, last = 0;
    const char *s = d;
    for (;;) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
            command = *s++;
            if (command == 'z' || command == 'Z') {
                c4fPathLine(p, ox + sx * scale, oy + sy * scale);
                x = sx; y = sy; last = command;
                continue;
            }
        } else if (!command) {
            break;
        }
        const char lower = command | 0x20;
        const int relative = command == lower, count = lower == 'h' || lower == 'v' ? 1 : lower == 'c' ? 6 :
                             lower == 's' || lower == 'q' ? 4 : 2;
        int read = 0;
        for (; read < count; read++) {
            while (*s == ' ' || *s == ',') s++;
            char *end;
            v[read] = strtof(s, &end);
            if (end == s) break;
            s = end;
        }
        if (read < count) break;
        float bx = relative ? x : 0, by = relative ? y : 0;
        if (lower == 'm') {
            x = bx + v[0]; y = by + v[1]; sx = x; sy = y;
            c4fPathMove(p, ox + x * scale, oy + y * scale);
            command = relative ? 'l' : 'L'; /* later pairs are lines */
        } else if (lower == 'l' || lower == 'h' || lower == 'v') {
            if (lower == 'l') { x = bx + v[0]; y = by + v[1]; }
            else if (lower == 'h') x = bx + v[0];
            else y = by + v[0];
            c4fPathLine(p, ox + x * scale, oy + y * scale);
        } else {
            float c1x, c1y, c2x, c2y, ex, ey;
            if (lower == 'c') {
                c1x = bx + v[0]; c1y = by + v[1]; c2x = bx + v[2]; c2y = by + v[3]; ex = bx + v[4]; ey = by + v[5];
            } else if (lower == 's') {
                int smooth = last == 'c' || last == 'C' || last == 's' || last == 'S';
                c1x = smooth ? 2 * x - cx : x; c1y = smooth ? 2 * y - cy : y;
                c2x = bx + v[0]; c2y = by + v[1]; ex = bx + v[2]; ey = by + v[3];
            } else { /* q: the quadratic as a cubic */
                float qx = bx + v[0], qy = by + v[1];
                ex = bx + v[2]; ey = by + v[3];
                c1x = x + 2.0f / 3 * (qx - x); c1y = y + 2.0f / 3 * (qy - y);
                c2x = ex + 2.0f / 3 * (qx - ex); c2y = ey + 2.0f / 3 * (qy - ey);
            }
            c4fPathCubic(p, ox + c1x * scale, oy + c1y * scale, ox + c2x * scale, oy + c2y * scale,
                         ox + ex * scale, oy + ey * scale);
            cx = c2x; cy = c2y; x = ex; y = ey;
        }
        last = command;
    }
}

static void c4fTextRight(C4fCanvas *c, int font, float size, float right, float y, C4fColor color, const char *text)
{
    c4fText(c, font, size, right - c4fTextWidth(font, size, text), y, color, text);
}

/* Letter-spaced capitals, centred, like the page's SHARE and OPTIONS. */
static void c4fSpacedCentered(C4fCanvas *c, float size, float cx, float y, float spacing, C4fColor color, const char *text)
{
    float width = 0;
    for (const char *t = text; *t; t++) {
        const char one[2] = { *t, 0 };
        width += c4fTextWidth(C4F_FONT_REGULAR, size, one) + (t[1] ? spacing : 0);
    }
    float x = cx - width / 2;
    for (const char *t = text; *t; t++) x += c4fTextN(c, C4F_FONT_REGULAR, size, x, y, color, t, 1) + spacing;
}

/* ---- the controller, at rest (the still layer) ---- */

static const C4fStop c4fPlastic[3] = { { 0, { 0x34, 0x3c, 0x4f, 255 } }, { .46f, { 0x1f, 0x25, 0x32, 255 } },
                                       { 1, { 0x15, 0x19, 0x23, 255 } } };

/* Glossy dark plastic lit from above: L2, L1, R2, R1, L3, R3 and the capsules. */
static void c4fPlasticShape(C4fCanvas *c, const C4fPath *outer, C4fBox b)
{
    c4fShadowPath(c, outer, c4fIdentity, 0, 6, 9, C4F_RGBA(0, 0, 0, .6f));
    c4fGradientPath(c, outer, c4fPlastic, 3, b.y, b.h, 0, 0, 0);
    c4fInsetPath(c, outer, 1, 0, 0, C4F_P_EDGE);
    c4fInsetPath(c, outer, 1, 1, 0, C4F_P_HIGHLIGHT);
}

static void c4fEngraved(C4fCanvas *c, C4fBox b, const char *label, C4fColor color)
{
    c4fTextCentered(c, C4F_FONT_REGULAR, 18, b.x + b.w / 2, b.y + b.h / 2 + 6.5f, color, label);
}

static const char *const c4fShoulderNames[4] = { "L2", "L1", "R2", "R1" };

static void c4fFaceCentre(int i, float *cx, float *cy, float *r)
{
    /* Triangle, Circle, Cross, Square: 34% wide, at the page's corners of the box. */
    static const float at[4][2] = { { .50f, .17f }, { .83f, .50f }, { .50f, .83f }, { .17f, .50f } };
    const C4fBox b = c4fControl(C4F_C_FACE);
    *cx = b.x + at[i][0] * b.w;
    *cy = b.y + at[i][1] * b.h;
    *r = .17f * b.w;
}

static C4fColor c4fFaceColor(int i)
{
    return i == 0 ? C4F_TRIANGLE_KEY : i == 1 ? C4F_CIRCLE_KEY : i == 2 ? C4F_CROSS_KEY : C4F_SQUARE_KEY;
}

static void c4fFaceSymbol(C4fCanvas *c, int i, float cx, float cy, float r, C4fColor color)
{
    /* The page's 24-unit symbols at 56% of the button. */
    const float s = 2 * r * .56f / 24, stroke = 1.7f * s;
    if (i == 0) {
        C4fPath tri = { 0 };
        c4fSvgPath(&tri, "M12 4.6l7.2 12.4H4.8z", cx - 12 * s, cy - 12 * s, s);
        c4fStrokePath(c, &tri, stroke, color);
        c4fPathFree(&tri);
    } else if (i == 1) {
        c4fRing(c, cx, cy, 6.6f * s, stroke, color);
    } else if (i == 2) {
        c4fSegment(c, cx - 5.4f * s, cy - 5.4f * s, cx + 5.4f * s, cy + 5.4f * s, stroke, color);
        c4fSegment(c, cx + 5.4f * s, cy - 5.4f * s, cx - 5.4f * s, cy + 5.4f * s, stroke, color);
    } else {
        c4fSquareOutline(c, cx, cy, 5.6f * s, stroke, color);
    }
}

static void c4fFaceButton(C4fCanvas *c, int i, float scale, int lit)
{
    float cx, cy, r;
    c4fFaceCentre(i, &cx, &cy, &r);
    r *= scale;
    C4fPath circle = { 0 };
    c4fPathCircle(&circle, cx, cy, r, 1);
    if (lit) c4fShadowPath(c, &circle, c4fIdentity, 0, 0, 12, c4fAlpha(c4fFaceColor(i), .7f));
    else c4fShadowPath(c, &circle, c4fIdentity, 0, 6, 9, C4F_RGBA(0, 0, 0, .6f));
    /* radial-gradient(circle at 50% 28%, #3c4458, #222837 48%, #141822): brighter when pressed. */
    const C4fStop rest[3] = { { 0, c4fHex(0x3c4458) }, { .48f, c4fHex(0x222837) }, { 1, c4fHex(0x141822) } };
    const C4fStop bright[3] = { { 0, c4fHex(0x4e5873) }, { .48f, c4fHex(0x2c3448) }, { 1, c4fHex(0x1a1f2c) } };
    c4fGradientPath(c, &circle, lit ? bright : rest, 3, 0, 0, cx, cy - .44f * r, 1.75f * r);
    c4fInsetPath(c, &circle, 1, 1, 0, C4F_P_HIGHLIGHT);
    if (lit) c4fInsetPath(c, &circle, 2, 0, 0, c4fFaceColor(i));
    else c4fRing(c, cx, cy, r + .5f, 1, C4F_RGBA(255, 255, 255, .05f));
    c4fFaceSymbol(c, i, cx, cy, r, c4fFaceColor(i));
    c4fPathFree(&circle);
}

/* The D-pad's four arms, from the page's SVG (viewBox -128 -128 256 256). */
static const char *const c4fArms[4] = {
    "M0-9L30-39V-88Q30-100 18-100H-18Q-30-100-30-88V-39Z",
    "M9 0L39 30H88Q100 30 100 18V-18Q100-30 88-30H39Z",
    "M0 9L-30 39V88Q-30 100-18 100H18Q30 100 30 88V39Z",
    "M-9 0L-39-30H-88Q-100-30-100-18V18Q-100 30-88 30H-39Z",
};

static void c4fArmPath(C4fPath *p, int arm)
{
    const C4fBox b = c4fControl(C4F_C_DPAD);
    c4fSvgPath(p, c4fArms[arm], b.x + b.w / 2, b.y + b.h / 2, b.w / 256);
}

static void c4fArm(C4fCanvas *c, int arm, int lit, C4fColor accent)
{
    const C4fBox b = c4fControl(C4F_C_DPAD);
    const float s = b.w / 256;
    C4fPath p = { 0 };
    c4fArmPath(&p, arm);
    if (lit) {
        const C4fStop fill[1] = { { 0, c4fMix(accent, c4fHex(0x232a3a), .42f) } };
        c4fShadowPath(c, &p, c4fIdentity, 0, 0, 10, c4fAlpha(accent, .55f));
        c4fGradientPath(c, &p, fill, 1, b.y, b.h, 0, 0, 0);
        c4fStrokePath(c, &p, 2 * s, accent);
    } else {
        const C4fStop fill[3] = { { 0, c4fHex(0x3d4558) }, { .5f, c4fHex(0x232a38) }, { 1, c4fHex(0x161a24) } };
        float top = b.y + b.h / 2 + (arm == 0 ? -100 : arm == 2 ? 9 : -30) * s;
        float height = (arm == 0 || arm == 2 ? 91 : 60) * s;
        c4fShadowPath(c, &p, c4fIdentity, 0, 5 * s, 5, C4F_RGBA(0, 0, 0, .55f));
        c4fGradientPath(c, &p, fill, 3, top, height, 0, 0, 0);
        c4fStrokePath(c, &p, 1.2f * s, C4F_RGBA(255, 255, 255, .13f));
    }
    c4fPathFree(&p);
}

static void c4fTouchpadRest(C4fCanvas *c)
{
    const C4fBox b = c4fControl(C4F_C_TOUCHPAD);
    const float u = c4fUnit();
    C4fPath p = { 0 };
    c4fShapePath(&p, C4F_C_TOUCHPAD, 0);
    c4fShadowPath(c, &p, c4fIdentity, 0, 10, 12, C4F_RGBA(0, 0, 0, .6f));
    const C4fStop plate[3] = { { 0, c4fHex(0x1d2331) }, { .7f, c4fHex(0x151a25) }, { 1, c4fHex(0x121620) } };
    c4fGradientPath(c, &p, plate, 3, b.y, b.h, 0, 0, 0);
    /* The fine dot grain, one dot every 1.5u. */
    unsigned char *mask;
    int x0, y0, w, h;
    if (c4fPathMask(c, &p, c4fIdentity, &mask, &x0, &y0, &w, &h)) {
        const float step = 1.5f * u;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (mask[y * w + x] < 255) continue;
                float fx = fmodf(x0 + x + .5f - b.x, step) - step / 2, fy = fmodf(y0 + y + .5f - b.y, step) - step / 2;
                float d = hypotf(fx, fy), cov = fmaxf(0, fminf(1, 1.2f - d));
                if (cov > 0) c4fBlend(c, x0 + x, y0 + y, C4F_RGBA(255, 255, 255, .06f), cov);
            }
        free(mask);
    }
    /* inset 0 -10px 24px rgba(0, 0, 0, .35): darker towards the bottom edge. */
    const C4fStop shade[2] = { { .72f, C4F_RGBA(0, 0, 0, 0) }, { 1, C4F_RGBA(0, 0, 0, .35f) } };
    c4fGradientPath(c, &p, shade, 2, b.y, b.h, 0, 0, 0);
    c4fInsetPath(c, &p, 1, 0, 0, C4F_RGBA(255, 255, 255, .07f));
    c4fPathFree(&p);
}

static void c4fPsButton(C4fCanvas *c)
{
    const C4fBox b = c4fControl(C4F_C_PS);
    const float u = c4fUnit(), cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w / 2;
    C4fPath p = { 0 };
    c4fPathCircle(&p, cx, cy, r + .6f * u + 1, 1);
    c4fShadowPath(c, &p, c4fIdentity, 0, 6, 9, C4F_RGBA(0, 0, 0, .6f));
    c4fFillCircle(c, cx, cy, r + .6f * u + 1, C4F_RGBA(255, 255, 255, .08f));
    c4fFillCircle(c, cx, cy, r + .6f * u, c4fHex(0x121620));
    c4fPathFree(&p);
    c4fPathCircle(&p, cx, cy, r, 1);
    const C4fStop cap[2] = { { 0, c4fHex(0x414a5f) }, { .72f, c4fHex(0x1a1f2b) } };
    c4fGradientPath(c, &p, cap, 2, 0, 0, cx, cy - .4f * r, 1.4f * r);
    c4fInsetPath(c, &p, 1, 1, 0, C4F_RGBA(255, 255, 255, .2f));
    c4fPathFree(&p);
    /* The PlayStation mark (Tokyoship, CC BY 3.0), viewBox 1240 1136 378 296, at 58%. */
    static const char *const mark[3] = {
        "m1442.9581 1196.8425-.242 234.3159-59.4601-20.4178-.4185-274.2048 75.8973 21.1864c48.5826 13.7861 78.3137 "
        "40.5845 77.8302 87.5507-.4829 54.6248-25.8649 75.8164-68.8533 55.8404v-101.2762c0-14.941-24.7536-25.8506-24.7536-2.9946z",
        "m1370.7136 1303.1522-8.9415 2.8809-86.5464 31.0437c-18.2193 8.3547-33.4371 13.7582-33.5335 27.3708.4569 17.2306 "
        "27.4593 25.6141 66.9974 32.2183 21.0698 4.2232 42.5958 1.3924 61.026-2.5123l.015-32.4651s-34.1651 13.278-42.2947 "
        "14.3829l-16.4389-.09c-12.2037-.067-17.8867-7.8843-5.7302-12.0759l65.4355-24.7626.011-35.9902z",
        "m1524.5024 1317.7768c-22.722-.075-54.1883 6.1167-69.7875 13.3735v37.0026l41.1316-15.041 32.581-10.3583c33.9325-5.5872 "
        "47.4232 6.2615 14.3731 16.1649l-88.0833 32.2321-.1072 35.8824 128.695-46.2998c14.2026-4.9527 34.177-14.9519 "
        "34.225-27.2419-3.4472-19.028-32.0697-30.1323-76.5199-34.8392-3.9869-.4111-7.9933-.6688-11.9728-.7879-1.5161-.044-3.0225-.083-4.5372-.088z",
    };
    const float size = .58f * b.w, scale = size / 378, top = cy - 296 * scale / 2;
    for (int i = 0; i < 3; i++) {
        C4fPath m = { 0 };
        c4fSvgPath(&m, mark[i], cx - size / 2 - 1240 * scale, top - 1136 * scale, scale);
        c4fFillPath(c, &m, c4fIdentity, C4F_RGBA(236, 240, 248, .75f));
        c4fPathFree(&m);
    }
}

static void c4fStickWell(C4fCanvas *c, int id)
{
    const C4fBox b = c4fControl(id);
    const float cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w / 2;
    C4fPath p = { 0 };
    c4fPathCircle(&p, cx, cy, r, 1);
    const C4fStop well[2] = { { 0, c4fHex(0x1d2333) }, { .7f, c4fHex(0x121621) } };
    c4fGradientPath(c, &p, well, 2, 0, 0, cx, cy - .1f * b.h, 1.08f * r);
    /* inset 0 8px 20px -10px rgba(0, 0, 0, .9): sunk at the top. */
    const C4fStop shade[2] = { { 0, C4F_RGBA(0, 0, 0, .45f) }, { .3f, C4F_RGBA(0, 0, 0, 0) } };
    c4fGradientPath(c, &p, shade, 2, b.y, b.h, 0, 0, 0);
    c4fInsetPath(c, &p, 1, 0, 0, C4F_RGBA(255, 255, 255, .07f));
    c4fPathFree(&p);
}

static void c4fBuildPicture(C4fCanvas *c)
{
    /* .pad-view's background: near black lit faintly from the top left. */
    C4fPath panel = { 0 };
    const C4fBox area = { C4F_P_X, C4F_P_Y, C4F_P_W, C4F_P_H };
    const float corners[8] = { 12, 12, 12, 12, 12, 12, 12, 12 };
    c4fRoundedPath(&panel, area, corners, 0);
    unsigned char *mask;
    int x0, y0, w, h;
    if (c4fPathMask(c, &panel, c4fIdentity, &mask, &x0, &y0, &w, &h)) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned m = mask[y * w + x];
                if (!m) continue;
                float px = x0 + x + .5f, py = y0 + y + .5f, rgb[3] = { 11, 14, 20 };
                /* radial-gradient(75% 95% at 10% -15%, accent 14%, transparent 62%) */
                float ex = (px - (area.x + .10f * area.w)) / (.75f * area.w), ey = (py - (area.y - .15f * area.h)) / (.95f * area.h);
                float a = .14f * fmaxf(0, 1 - sqrtf(ex * ex + ey * ey) / .62f);
                rgb[0] += (58 - rgb[0]) * a; rgb[1] += (140 - rgb[1]) * a; rgb[2] += (255 - rgb[2]) * a;
                /* radial-gradient(65% 80% at 105% 112%, rgba(120, 140, 200, .08), transparent 62%) */
                ex = (px - (area.x + 1.05f * area.w)) / (.65f * area.w); ey = (py - (area.y + 1.12f * area.h)) / (.8f * area.h);
                a = .08f * fmaxf(0, 1 - sqrtf(ex * ex + ey * ey) / .62f);
                rgb[0] += (120 - rgb[0]) * a; rgb[1] += (140 - rgb[1]) * a; rgb[2] += (200 - rgb[2]) * a;
                c4fBlend(c, x0 + x, y0 + y, (C4fColor){ (uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2], 255 }, m / 255.0f);
            }
        free(mask);
    }
    c4fInsetPath(c, &panel, 1, 0, 0, C4F_LINE_SOFT);
    c4fPathFree(&panel);

    const float u = c4fUnit();
    for (int i = 0; i < 4; i++) {
        C4fPath p = { 0 };
        c4fShapePath(&p, i, 0);
        c4fPlasticShape(c, &p, c4fControl(i));
        c4fEngraved(c, c4fControl(i), c4fShoulderNames[i], C4F_P_ENGRAVED);
        c4fPathFree(&p);
    }
    for (int id = C4F_C_SHARE; id <= C4F_C_OPTIONS; id++) {
        const C4fBox b = c4fControl(id), cap = c4fCapsule(id);
        c4fSpacedCentered(c, 13, b.x + b.w / 2, cap.y - 1.1f * u - 2, 1.6f, C4F_P_ENGRAVED,
                          id == C4F_C_SHARE ? "SHARE" : "OPTIONS");
        C4fPath p = { 0 };
        c4fCapsulePath(&p, id, 0);
        c4fPlasticShape(c, &p, cap);
        c4fPathFree(&p);
    }
    c4fTouchpadRest(c);
    {
        /* The pivot ring and the printed direction marks around the arms. */
        const C4fBox b = c4fControl(C4F_C_DPAD);
        const float s = b.w / 256, cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        c4fRing(c, cx, cy, 34 * s, 1.5f * s, C4F_RGBA(255, 255, 255, .07f));
        C4fPath marks = { 0 };
        c4fSvgPath(&marks, "M0-122l12 14h-24zM0 122l12-14h-24zM-122 0l14 12v-24zM122 0l-14 12v-24z", cx, cy, s);
        c4fStrokePath(c, &marks, 2.2f * s, C4F_P_PRINT);
        c4fPathFree(&marks);
        for (int arm = 0; arm < 4; arm++) c4fArm(c, arm, 0, C4F_ACCENT);
    }
    for (int i = 0; i < 4; i++) c4fFaceButton(c, i, 1, 0);
    c4fPsButton(c);
    c4fStickWell(c, C4F_C_LSTICK);
    c4fStickWell(c, C4F_C_RSTICK);
    for (int id = C4F_C_L3; id <= C4F_C_R3; id++) {
        C4fPath p = { 0 };
        c4fShapePath(&p, id, 0);
        c4fPlasticShape(c, &p, c4fControl(id));
        c4fEngraved(c, c4fControl(id), id == C4F_C_L3 ? "L3" : "R3", C4F_P_ENGRAVED);
        c4fPathFree(&p);
    }
}

static void c4fBuildLayer(void)
{
    const size_t bytes = (size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT * sizeof(uint32_t);
    uint32_t *px = c4fTakeCleanBackdrop();
    C4fCanvas c = { px, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT };
    if (!px) {
        if (!(px = malloc(bytes))) return;
        c.px = px;
        c4fPaintBackdrop(&c, C4F_S);
    }
    /* Preserve the clean waves before the live controller is painted. Opening
     * tools later must not regenerate a whole-screen gradient and wave paths. */
    if (!c4fToolsBase) {
        c4fToolsBase = malloc(bytes);
        if (c4fToolsBase) memcpy(c4fToolsBase, px, bytes);
    }
    /* .tile has backdrop-filter: blur(6px). */
    c4fBlurUnder(&c, C4F_T_LEFT, C4F_T_TOP, C4F_T_PAD_W, C4F_T_HEIGHT, 6 * C4F_S, 6 * C4F_S);
    c4fBlurUnder(&c, C4F_T_INFO_X, C4F_T_TOP, C4F_T_INFO_W, C4F_T_HEIGHT, 6 * C4F_S, 6 * C4F_S);

    /* .topbar */
    c4fMark(&c, 64 + 24, 54, 48, C4F_WHITE);
    float x = 64 + 48 + 16;
    x += c4fText(&c, C4F_FONT_LIGHT, 17 * C4F_S, x, 63, C4F_TEXT, "Control4Free") + 14 * C4F_S;
    c4fDot(&c, x + 4 * C4F_S, 55, C4F_ACCENT, 0);
    c4fText(&c, C4F_FONT_LIGHT, 13 * C4F_S, x + 16 * C4F_S, 62, C4F_MUTED, "Controller test");
    c4fDrawSolidarityBadge(&c, C4F_SCREEN_WIDTH - 64, 40);

    c4fText(&c, C4F_FONT_LIGHT, 48, C4F_T_LEFT, 140, C4F_TEXT, "Controller test");
    c4fTextWrapped(&c, C4F_FONT_LIGHT, 24, C4F_T_LEFT, 184, C4F_T_RIGHT - C4F_T_LEFT, 34, 2, C4F_MUTED,
                   "See the controller input received by this PS4. Select a user, then press buttons or move "
                   "the sticks. PS and Share go to the PS4 itself and aren't shown here.");

    c4fTile(&c, C4F_T_LEFT, C4F_T_TOP, C4F_T_PAD_W, C4F_T_HEIGHT, C4F_ACCENT);
    c4fTile(&c, C4F_T_INFO_X, C4F_T_TOP, C4F_T_INFO_W, C4F_T_HEIGHT, C4F_ACCENT);
    c4fBuildPicture(&c);

    /* The panel's headings and labels. */
    const float tx = C4F_T_TEXT_X;
    c4fText(&c, C4F_FONT_REGULAR, 24, tx, C4F_T_ROW_STICKS, C4F_MUTED, "Sticks");
    c4fText(&c, C4F_FONT_LIGHT, 26, tx, C4F_T_ROW_STICKS + 40, C4F_TEXT, "Left");
    c4fText(&c, C4F_FONT_LIGHT, 26, tx, C4F_T_ROW_STICKS + 76, C4F_TEXT, "Right");
    c4fText(&c, C4F_FONT_REGULAR, 24, tx, C4F_T_ROW_TRIGGERS, C4F_MUTED, "Triggers");
    for (int i = 0; i < 2; i++) {
        float y = C4F_T_ROW_TRIGGERS + 40 + i * 36;
        c4fText(&c, C4F_FONT_LIGHT, 26, tx, y, C4F_TEXT, i ? "R2" : "L2");
        c4fFillRoundRect(&c, C4F_T_BAR_X, y - 14, C4F_T_BAR_W, 14, 7, C4F_RGBA(0, 0, 0, .35f));
    }
    c4fText(&c, C4F_FONT_REGULAR, 24, tx, C4F_T_ROW_TOUCH, C4F_MUTED, "Touchpad");
    c4fText(&c, C4F_FONT_REGULAR, 24, tx, C4F_T_ROW_MOTION, C4F_MUTED, "Motion");
    static const char *columns[4] = { "x", "y", "z", "w" };
    for (int i = 0; i < 4; i++)
        c4fTextRight(&c, C4F_FONT_REGULAR, 22, C4F_T_COLUMN_X + (i + 1) * C4F_T_COLUMN_W, C4F_T_ROW_MOTION,
                     C4F_FAINT, columns[i]);
    c4fText(&c, C4F_FONT_LIGHT, 24, tx, C4F_T_ROW_MOTION + 40, C4F_TEXT, "Acceleration (G)");
    c4fText(&c, C4F_FONT_LIGHT, 24, tx, C4F_T_ROW_MOTION + 76, C4F_TEXT, "Gyro (rad/s)");
    c4fText(&c, C4F_FONT_LIGHT, 24, tx, C4F_T_ROW_MOTION + 112, C4F_TEXT, "Orientation");
    c4fText(&c, C4F_FONT_REGULAR, 24, tx, C4F_T_ROW_LIGHT, C4F_MUTED, "Light bar");

    c4fText(&c, C4F_FONT_LIGHT, 20, C4F_T_LEFT, 1030, C4F_FAINT, "Control4Free " C4F_LAUNCHER_VERSION);
    c4fTextRight(&c, C4F_FONT_LIGHT, 23, C4F_T_RIGHT, 1030, C4F_MUTED,
                 "Hold L3 + R3: menu    TV remote: OK menu / Back leave");
    c4fLayer = px;
}

/* ---- what the controller is doing ---- */

static C4fColor c4fStateColor(int state)
{
    switch (state) {
    case C4F_TESTER_READY: return C4F_OK;
    case C4F_TESTER_NO_CONTROLLER: return C4F_FAINT;
    case C4F_TESTER_FAILED: return C4F_BAD;
    default: return C4F_WARN;
    }
}

static void c4fDrawTabs(C4fCanvas *c, const C4fTesterView *v)
{
    const float y = C4F_T_TABS_Y, h = 50;
    float x = C4F_T_LEFT;
    int any = 0;
    for (int i = 0; i < C4F_TESTER_USERS; i++) {
        if (!v->user[i].used) continue;
        int on = i == v->selected;
        float w = c4fTextWidth(C4F_FONT_REGULAR, 26, v->user[i].name) + 84;
        any = 1;
        c4fFillRoundRect(c, x, y, w, h, 6 * C4F_S, on ? C4F_RGBA(40, 103, 205, .85f) : C4F_TILE);
        c4fInsetRoundRect(c, x, y, w, h, 6 * C4F_S, on ? 3 : 1 * C4F_S, on ? C4F_ACCENT : C4F_LINE);
        c4fDot(c, x + 28, y + h / 2, c4fStateColor(v->user[i].state), v->user[i].state == C4F_TESTER_READY);
        c4fText(c, C4F_FONT_REGULAR, 26, x + 52, y + h / 2 + 9, on ? C4F_TEXT : C4F_MUTED, v->user[i].name);
        x += w + 16;
    }
    if (!any) c4fText(c, C4F_FONT_LIGHT, 26, C4F_T_LEFT, y + 34, C4F_MUTED, "Waiting for the PS4's signed-in users...");
}

/* A pressed plastic control: lit in the accent colour, like the page's .on. */
static void c4fPlasticLit(C4fCanvas *c, const C4fPath *p, C4fBox b, C4fColor accent)
{
    const C4fStop lit[2] = { { 0, c4fMix(accent, c4fHex(0x2a3142), .40f) }, { 1, c4fMix(accent, c4fHex(0x151923), .22f) } };
    c4fShadowPath(c, p, c4fIdentity, 0, 0, 10, c4fAlpha(accent, .6f));
    c4fGradientPath(c, p, lit, 2, b.y, b.h, 0, 0, 0);
    c4fInsetPath(c, p, 2, 0, 0, c4fAlpha(accent, .75f));
}

/* L2 / R2: the fill rises with the pressure, inside the trigger's shape. */
static void c4fTriggerFill(C4fCanvas *c, int id, int pressure, C4fColor accent)
{
    if (pressure <= 0) return;
    const C4fBox b = c4fControl(id);
    const float top = b.y + b.h * (1 - pressure / 255.0f);
    C4fMask *mask = &c4fTriggerMasks[id == C4F_C_R2];
    if (!mask->px) {
        C4fPath p = { 0 };
        c4fShapePath(&p, id, 0);
        c4fPathMask(c, &p, c4fIdentity, &mask->px, &mask->x, &mask->y, &mask->w, &mask->h);
        c4fPathFree(&p);
    }
    if (mask->px) {
        for (int y = 0; y < mask->h; y++) {
            float rowTop = mask->y + y, cover = fmaxf(0, fminf(1, rowTop + 1 - top));
            if (cover <= 0) continue;
            for (int x = 0; x < mask->w; x++)
                if (mask->px[y * mask->w + x]) c4fBlend(c, mask->x + x, mask->y + y, c4fAlpha(accent, .42f),
                                                      cover * mask->px[y * mask->w + x] / 255.0f);
        }
    }
}

static void c4fPaintKnob(C4fCanvas *c, float cx, float cy, float radius, int clicked, int highlighted, C4fColor accent)
{
    const float r = radius * (clicked ? .9f : 1);
    C4fPath p = { 0 };
    c4fPathCircle(&p, cx, cy, r, 1);
    c4fShadowPath(c, &p, c4fIdentity, 0, 8, 9, C4F_RGBA(0, 0, 0, .7f));
    if (highlighted) c4fShadowPath(c, &p, c4fIdentity, 0, 0, 9, c4fAlpha(accent, .65f));
    const C4fStop body[3] = { { 0, c4fHex(clicked ? 0x56607b : 0x3f475b) }, { .55f, c4fHex(clicked ? 0x323a4d : 0x252b39) },
                              { 1, c4fHex(clicked ? 0x1e2331 : 0x161a24) } };
    c4fGradientPath(c, &p, body, 3, cy - r, 2 * r, 0, 0, 0);
    /* The ridged rim: repeating-conic-gradient, 4 degree stripes. */
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++)
        for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
            float dx = x + .5f - cx, dy = y + .5f - cy, d = hypotf(dx, dy);
            if (d > r) continue;
            float degrees = atan2f(dx, -dy) * 57.29578f + 360;
            if (fmodf(degrees, 8) < 4) c4fBlend(c, x, y, C4F_RGBA(255, 255, 255, .045f), fminf(1, r - d));
        }
    c4fInsetPath(c, &p, 1, 1, 0, C4F_RGBA(255, 255, 255, .2f));
    c4fRing(c, cx, cy, r + .5f, 1, highlighted ? c4fAlpha(accent, .8f) : C4F_RGBA(0, 0, 0, .55f));
    c4fPathFree(&p);
    /* The thumb dish: dark at the top and lit at the bottom, so it reads as hollow. */
    const float dish = r * .66f;
    c4fPathCircle(&p, cx, cy, dish, 1);
    const C4fStop hollow[3] = { { 0, c4fHex(0x0e1118) }, { .55f, c4fHex(0x1a1f2b) }, { 1, c4fHex(0x2b3242) } };
    c4fGradientPath(c, &p, hollow, 3, cy - dish, 2 * dish, 0, 0, 0);
    c4fRing(c, cx, cy, dish + .5f, 1, C4F_RGBA(0, 0, 0, .45f));
    c4fPathFree(&p);
}

static void c4fPaintLightStrip(C4fCanvas *c, C4fColor accent)
{
    const C4fBox pad = c4fControl(C4F_C_TOUCHPAD);
    const float u = c4fUnit(), sx = pad.x + .07f * pad.w, sw = .86f * pad.w, sh = fmaxf(3, .8f * u);
    c4fShadowRoundRect(c, sx, pad.y - 2, sw, sh + 2, 2, 0, -2, 3.5f * u, 0, c4fAlpha(accent, .55f));
    c4fShadowRoundRect(c, sx, pad.y, sw, sh, 2, 0, 0, 2.5f * u, 0, c4fAlpha(accent, .9f));
    c4fFillRoundRect(c, sx, pad.y, sw, sh, sh / 2, accent);
}

static void c4fPaintTouch(C4fCanvas *c, float x, float y, C4fColor accent)
{
    const float r = 3.5f * c4fUnit();
    c4fGlowCircle(c, x, y, r, 20, 0, c4fAlpha(accent, .8f));
    c4fFillCircle(c, x, y, r, c4fAlpha(accent, .45f));
    c4fRing(c, x, y, r, 1, c4fAlpha(accent, .85f));
}

/* One transparent sprite per pressed control. Overlapping glows still compose
 * in their original order; the static controller is never baked into a sprite. */
static void c4fPaintPressed(C4fCanvas *c, int index, C4fColor accent)
{
    if (index < 4 || index >= 14) {
        const int id = index < 4 ? index : index == 14 ? C4F_C_L3 : C4F_C_R3;
        C4fPath p = { 0 };
        c4fShapePath(&p, id, 0);
        c4fPlasticLit(c, &p, c4fControl(id), accent);
        if (index >= 14) c4fEngraved(c, c4fControl(id), index == 14 ? "L3" : "R3", C4F_P_ENGRAVED);
        c4fPathFree(&p);
    } else if (index == 4) {
        C4fPath p = { 0 };
        c4fCapsulePath(&p, C4F_C_OPTIONS, 0);
        c4fPlasticLit(c, &p, c4fCapsule(C4F_C_OPTIONS), accent);
        c4fPathFree(&p);
    } else if (index < 9) {
        c4fArm(c, index - 5, 1, accent);
    } else if (index < 13) {
        c4fFaceButton(c, index - 9, 1, 1);
    } else {
        C4fPath p = { 0 };
        c4fShapePath(&p, C4F_C_TOUCHPAD, 0);
        c4fInsetPath(c, &p, 2, 0, 0, c4fAlpha(accent, .7f));
        c4fPathFree(&p);
    }
}

static const uint32_t c4fPressedBits[16] = {
    C4F_INPUT_BUTTON_L2, C4F_INPUT_BUTTON_L1, C4F_INPUT_BUTTON_R2, C4F_INPUT_BUTTON_R1, C4F_INPUT_BUTTON_OPTIONS,
    C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_RIGHT, C4F_INPUT_BUTTON_DOWN, C4F_INPUT_BUTTON_LEFT,
    C4F_INPUT_BUTTON_TRIANGLE, C4F_INPUT_BUTTON_CIRCLE, C4F_INPUT_BUTTON_CROSS, C4F_INPUT_BUTTON_SQUARE,
    C4F_INPUT_BUTTON_TOUCHPAD, C4F_INPUT_BUTTON_L3, C4F_INPUT_BUTTON_R3,
};

/* The drawing API blends onto an opaque destination. Black/white matte renders
 * recover a premultiplied sprite once, including antialiasing and soft shadows.
 * The caller's frame is scratch here, before the still layer is copied into it. */
static void c4fPaintSprite(C4fCanvas *c, int kind, int index, C4fColor accent)
{
    if (kind == 0) c4fPaintKnob(c, 100, 100, c4fControl(C4F_C_LSTICK).w / 4, index == 2, index != 0, accent);
    else if (kind == 1) c4fPaintPressed(c, index, accent);
    else if (kind == 2) c4fPaintTouch(c, 100, 100, accent);
    else c4fPaintLightStrip(c, accent);
}

static void c4fBuildSprite(C4fCanvas *work, C4fSprite *s, C4fBox box, int kind, int index, C4fColor accent)
{
    if (s->px) return;
    s->x = (int)floorf(fmaxf(0, box.x)); s->y = (int)floorf(fmaxf(0, box.y));
    s->w = (int)ceilf(fminf(work->w, box.x + box.w)) - s->x;
    s->h = (int)ceilf(fminf(work->h, box.y + box.h)) - s->y;
    if (s->w <= 0 || s->h <= 0) return;
    s->px = malloc((size_t)s->w * s->h * sizeof(uint32_t));
    if (!s->px) return;
    if (!c4fSpriteScratch) c4fSpriteScratch = malloc((size_t)work->w * work->h * sizeof(uint32_t));
    if (!c4fSpriteScratch) { free(s->px); s->px = NULL; return; }
    C4fCanvas black = { c4fSpriteScratch, work->w, work->h };
    /* Painters may sample/paint outside the capture rectangle. Initializing the
     * complete matte avoids undefined reads even if their footprint changes. */
    for (int i = 0; i < work->w * work->h; i++) black.px[i] = 0xff000000;
    c4fPaintSprite(&black, kind, index, accent);
    for (int y = 0; y < s->h; y++)
        memcpy(s->px + y * s->w, black.px + (s->y + y) * black.w + s->x, (size_t)s->w * sizeof(uint32_t));
    for (int i = 0; i < work->w * work->h; i++) work->px[i] = 0xffffffff;
    c4fPaintSprite(work, kind, index, accent);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            const uint32_t b = s->px[y * s->w + x], w = work->px[(s->y + y) * work->w + s->x + x];
            int a = 255 - ((int)((w >> 8) & 255) - (int)((b >> 8) & 255));
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            const unsigned r = (b >> 16) & 255, g = (b >> 8) & 255, blue = b & 255;
            s->px[y * s->w + x] = (uint32_t)a << 24 | (r > (unsigned)a ? (unsigned)a : r) << 16 |
                                  (g > (unsigned)a ? (unsigned)a : g) << 8 | (blue > (unsigned)a ? (unsigned)a : blue);
        }
}

static void c4fBlitSprite(C4fCanvas *c, const C4fSprite *s, int dx, int dy)
{
    for (int y = 0; y < s->h; y++) {
        const int py = s->y + y + dy;
        if (py < 0 || py >= c->h) continue;
        for (int x = 0; x < s->w; x++) {
            const int px = s->x + x + dx;
            if (px < 0 || px >= c->w) continue;
            const uint32_t src = s->px[y * s->w + x];
            const unsigned a = src >> 24;
            if (!a) continue;
            uint32_t *dst = c->px + py * c->w + px;
            if (a == 255) { *dst = src; continue; }
            const unsigned inv = 255 - a;
            const unsigned r = ((src >> 16) & 255) + (((*dst >> 16) & 255) * inv + 127) / 255;
            const unsigned g = ((src >> 8) & 255) + (((*dst >> 8) & 255) * inv + 127) / 255;
            const unsigned b = (src & 255) + ((*dst & 255) * inv + 127) / 255;
            *dst = 0xff000000 | r << 16 | g << 8 | b;
        }
    }
}

static C4fBox c4fPressedBounds(int index)
{
    C4fBox b;
    if (index < 4) b = c4fControl(index);
    else if (index == 4) b = c4fCapsule(C4F_C_OPTIONS);
    else if (index < 9) b = c4fControl(C4F_C_DPAD);
    else if (index < 13) {
        float x, y, r;
        c4fFaceCentre(index - 9, &x, &y, &r);
        b = (C4fBox){ x - r, y - r, 2 * r, 2 * r };
    } else b = c4fControl(index == 13 ? C4F_C_TOUCHPAD : index == 14 ? C4F_C_L3 : C4F_C_R3);
    return (C4fBox){ b.x - 40, b.y - 40, b.w + 80, b.h + 80 };
}

static void c4fPrepareSprites(C4fCanvas *work, const C4fTesterView *v, C4fColor accent, int ready)
{
    const uint32_t rgb = (uint32_t)accent.r << 16 | (uint32_t)accent.g << 8 | accent.b;
    if (!c4fHasSpriteAccent || c4fSpriteAccent != rgb) {
        for (int i = 0; i < 3; i++) { free(c4fKnobs[i].px); c4fKnobs[i].px = NULL; }
        for (int i = 0; i < 16; i++) { free(c4fPressed[i].px); c4fPressed[i].px = NULL; }
        free(c4fTouchGlow.px); c4fTouchGlow.px = NULL;
        free(c4fLightStrip.px); c4fLightStrip.px = NULL;
        c4fSpriteAccent = rgb; c4fHasSpriteAccent = 1;
    }
    const C4fBox pad = c4fControl(C4F_C_TOUCHPAD);
    c4fBuildSprite(work, &c4fLightStrip, (C4fBox){ pad.x - 30, pad.y - 55, pad.w + 60, 110 }, 3, 0, accent);
    if (ready) {
        /* All three knobs are warmed together: moving/clicking a stick should
         * never introduce a first-use shadow/ridge calculation into a frame. */
        for (int i = 0; i < 3; i++) c4fBuildSprite(work, &c4fKnobs[i], (C4fBox){ 30, 30, 140, 150 }, 0, i, accent);
        for (int i = 0; i < 16; i++) if (v->buttons & c4fPressedBits[i])
            c4fBuildSprite(work, &c4fPressed[i], c4fPressedBounds(i), 1, i, accent);
        if (v->touches) c4fBuildSprite(work, &c4fTouchGlow, (C4fBox){ 45, 45, 110, 110 }, 2, 0, accent);
    }
    free(c4fSpriteScratch); c4fSpriteScratch = NULL;
}

static void c4fKnob(C4fCanvas *c, int id, int sx, int sy, int clicked, C4fColor accent)
{
    const C4fBox b = c4fControl(id);
    const float cx = b.x + b.w / 2 + (sx - 128) / 128.0f * b.w * C4F_KNOB_TRAVEL;
    const float cy = b.y + b.h / 2 + (sy - 128) / 128.0f * b.h * C4F_KNOB_TRAVEL;
    const int moved = abs(sx - 128) > 3 || abs(sy - 128) > 3, state = clicked ? 2 : moved ? 1 : 0;
    if (c4fKnobs[state].px) c4fBlitSprite(c, &c4fKnobs[state], (int)lroundf(cx) - 100, (int)lroundf(cy) - 100);
    else c4fPaintKnob(c, cx, cy, b.w / 4, clicked, moved || clicked, accent);
}

static void c4fDrawPressed(C4fCanvas *c, int index, C4fColor accent)
{
    if (c4fPressed[index].px) c4fBlitSprite(c, &c4fPressed[index], 0, 0);
    else c4fPaintPressed(c, index, accent);
}

static void c4fDrawInput(C4fCanvas *c, const C4fTesterView *v, C4fColor accent)
{
    const uint32_t b = v->buttons;
    static const uint32_t shoulders[4] = { C4F_INPUT_BUTTON_L2, C4F_INPUT_BUTTON_L1, C4F_INPUT_BUTTON_R2, C4F_INPUT_BUTTON_R1 };
    for (int i = 0; i < 4; i++) {
        if (b & shoulders[i]) c4fDrawPressed(c, i, accent);
        if (i == C4F_C_L2 || i == C4F_C_R2) c4fTriggerFill(c, i, i == C4F_C_L2 ? v->l2 : v->r2, accent);
        if ((b & shoulders[i]) || (i == C4F_C_L2 && v->l2) || (i == C4F_C_R2 && v->r2))
            c4fEngraved(c, c4fControl(i), c4fShoulderNames[i], C4F_P_ENGRAVED);
    }
    if (b & C4F_INPUT_BUTTON_OPTIONS) c4fDrawPressed(c, 4, accent);
    static const uint32_t arms[4] = { C4F_INPUT_BUTTON_UP, C4F_INPUT_BUTTON_RIGHT, C4F_INPUT_BUTTON_DOWN, C4F_INPUT_BUTTON_LEFT };
    for (int arm = 0; arm < 4; arm++) if (b & arms[arm]) c4fDrawPressed(c, 5 + arm, accent);
    static const uint32_t faces[4] = { C4F_INPUT_BUTTON_TRIANGLE, C4F_INPUT_BUTTON_CIRCLE, C4F_INPUT_BUTTON_CROSS, C4F_INPUT_BUTTON_SQUARE };
    for (int i = 0; i < 4; i++) if (b & faces[i]) c4fDrawPressed(c, 9 + i, accent);
    const C4fBox pad = c4fControl(C4F_C_TOUCHPAD);
    if (b & C4F_INPUT_BUTTON_TOUCHPAD) c4fDrawPressed(c, 13, accent);
    for (int i = 0; i < v->touches && i < 2; i++) {
        float tx = pad.x + pad.w * (v->touchX[i] / 1919.0f), ty = pad.y + pad.h * (v->touchY[i] / 941.0f);
        if (c4fTouchGlow.px) c4fBlitSprite(c, &c4fTouchGlow, (int)lroundf(tx) - 100, (int)lroundf(ty) - 100);
        else c4fPaintTouch(c, tx, ty, accent);
    }
    c4fKnob(c, C4F_C_LSTICK, v->lx, v->ly, !!(b & C4F_INPUT_BUTTON_L3), accent);
    c4fKnob(c, C4F_C_RSTICK, v->rx, v->ry, !!(b & C4F_INPUT_BUTTON_R3), accent);
    for (int id = C4F_C_L3; id <= C4F_C_R3; id++) {
        if (!(b & (id == C4F_C_L3 ? C4F_INPUT_BUTTON_L3 : C4F_INPUT_BUTTON_R3))) continue;
        c4fDrawPressed(c, id == C4F_C_L3 ? 14 : 15, accent);
    }
}

/* The touchpad's light strip along its top edge, and its name label, as the page shows them. */
static void c4fDrawTouchpad(C4fCanvas *c, C4fColor accent, const char *line1, const char *line2)
{
    const C4fBox pad = c4fControl(C4F_C_TOUCHPAD);
    if (c4fLightStrip.px) c4fBlitSprite(c, &c4fLightStrip, 0, 0);
    else c4fPaintLightStrip(c, accent);
    if (line1) c4fTextCentered(c, C4F_FONT_LIGHT, 22, pad.x + pad.w / 2, pad.y + pad.h * .9f - 30, C4F_P_ENGRAVED, line1);
    if (line2) c4fTextCentered(c, C4F_FONT_LIGHT, 20, pad.x + pad.w / 2, pad.y + pad.h * .9f - 4,
                               C4F_RGBA(236, 240, 248, .36f), line2);
}

static void c4fDrawValues(C4fCanvas *c, const C4fTesterView *v, int ready)
{
    const float tx = C4F_T_TEXT_X;
    char text[96];
    for (int i = 0; i < 2; i++) {
        if (ready) snprintf(text, sizeof(text), "x %3u    y %3u", (unsigned)(i ? v->rx : v->lx), (unsigned)(i ? v->ry : v->ly));
        else snprintf(text, sizeof(text), "-");
        c4fText(c, C4F_FONT_REGULAR, 26, tx + 120, C4F_T_ROW_STICKS + 40 + i * 36, C4F_TEXT, text);
    }
    for (int i = 0; i < 2; i++) {
        unsigned value = i ? v->r2 : v->l2;
        float y = C4F_T_ROW_TRIGGERS + 40 + i * 36;
        if (ready && value) c4fFillRoundRect(c, C4F_T_BAR_X, y - 14, C4F_T_BAR_W * value / 255.0f, 14, 7, C4F_ACCENT);
        if (ready) snprintf(text, sizeof(text), "%u", value);
        else snprintf(text, sizeof(text), "-");
        c4fText(c, C4F_FONT_REGULAR, 26, C4F_T_BAR_X + C4F_T_BAR_W + 20, y, C4F_TEXT, text);
    }
    if (!ready) snprintf(text, sizeof(text), "-");
    else if (!v->touches) snprintf(text, sizeof(text), "No touch");
    else if (v->touches == 1) snprintf(text, sizeof(text), "%u, %u", (unsigned)v->touchX[0], (unsigned)v->touchY[0]);
    else snprintf(text, sizeof(text), "%u, %u    %u, %u", (unsigned)v->touchX[0], (unsigned)v->touchY[0],
                  (unsigned)v->touchX[1], (unsigned)v->touchY[1]);
    c4fText(c, C4F_FONT_REGULAR, 26, tx + 140, C4F_T_ROW_TOUCH, C4F_TEXT, text);

    for (int row = 0; row < 3; row++) {
        int count = row == 2 ? 4 : 3;
        for (int i = 0; i < count; i++) {
            if (!ready) snprintf(text, sizeof(text), "-");
            else if (row == 0) snprintf(text, sizeof(text), "%.2f", v->accel[i] / 100.0);
            else if (row == 1) snprintf(text, sizeof(text), "%.2f", v->gyro[i] / 100.0);
            else snprintf(text, sizeof(text), "%.2f", v->quat[i] / 1000.0);
            c4fTextRight(c, C4F_FONT_REGULAR, 24, C4F_T_COLUMN_X + (i + 1) * C4F_T_COLUMN_W,
                         C4F_T_ROW_MOTION + 40 + row * 36, C4F_TEXT, text);
        }
    }

    if (v->lightSet) {
        C4fColor color = { v->light[0], v->light[1], v->light[2], 255 };
        c4fFillRoundRect(c, tx + 140, C4F_T_ROW_LIGHT - 22, 44, 26, 6, color);
        c4fText(c, C4F_FONT_LIGHT, 24, tx + 198, C4F_T_ROW_LIGHT, C4F_TEXT, "Set by this test");
    } else {
        c4fText(c, C4F_FONT_LIGHT, 24, tx + 140, C4F_T_ROW_LIGHT, C4F_MUTED, "As the PS4 sets it");
    }
}

/* Browser-reported round trips, as on the controller page. This is separate
 * from the native pad samples and does not measure input or TV display delay. */
static void c4fDrawConnection(C4fCanvas *c, const C4fTesterView *v, const C4fTesterConnection *connection,
                             float x, float baseline)
{
    c4fText(c, C4F_FONT_LIGHT, 20, x, baseline, C4F_MUTED, "Controller page RTT");
    const float values = x + 236;
    if (v->selected < 0 || v->selected >= C4F_TESTER_USERS || !connection->available) {
        c4fText(c, C4F_FONT_REGULAR, 22, values, baseline, C4F_FAINT, "--");
    } else if (!(connection->pads & 15U)) {
        c4fText(c, C4F_FONT_LIGHT, 22, values, baseline, C4F_MUTED, "No controller page for this user");
    } else {
        int column = 0;
        for (int slot = 0; slot < 4; slot++) {
            if (!(connection->pads & (1U << slot))) continue;
            char text[40];
            if (connection->rttMs[slot] < 0) snprintf(text, sizeof(text), "C%d measuring...", slot + 1);
            else snprintf(text, sizeof(text), "C%d %d ms", slot + 1, connection->rttMs[slot]);
            c4fText(c, C4F_FONT_REGULAR, 22, values + column++ * 176, baseline,
                    connection->rttMs[slot] < 0 ? C4F_MUTED : C4F_TEXT, text);
        }
    }
}

/* ---- dedicated tools view ---- */

#define C4F_TOOL_W 386
#define C4F_TOOL_H 220
#define C4F_TOOL_GAP_X 25
#define C4F_TOOL_GAP_Y 28
#define C4F_TOOL_Y 360

static C4fSprite c4fToolTiles[C4F_TESTER_ITEMS][4];
static C4fTesterView c4fToolsKey;
static C4fTesterConnection c4fToolsConnection;
static unsigned c4fToolsAvailable;
static int c4fHasToolsKey;

static C4fBox c4fToolBox(int item)
{
    return (C4fBox){ C4F_T_LEFT + (item % C4F_TESTER_MENU_COLUMNS) * (C4F_TOOL_W + C4F_TOOL_GAP_X),
                     C4F_TOOL_Y + (item / C4F_TESTER_MENU_COLUMNS) * (C4F_TOOL_H + C4F_TOOL_GAP_Y),
                     C4F_TOOL_W, C4F_TOOL_H };
}

/* Small, consistent line icons, drawn once into the tile cache. */
static void c4fToolIcon(C4fCanvas *c, int item, float x, float y, C4fColor color)
{
    static const char *const icons[C4F_TESTER_ITEMS] = {
        "M21 14L49 32L21 50Z",
        "M31 17C31 22 27 26 22 26C17 26 13 22 13 17C13 12 17 8 22 8C27 8 31 12 31 17Z "
        "M7 48V43C7 36 14 31 22 31C30 31 37 36 37 43V48 M40 11C46 11 51 15 51 21C51 26 47 30 42 31 "
        "M43 41H60M54 35L60 41L54 47",
        "M53 25C50 14 39 8 28 11C17 14 11 25 14 36C17 47 29 53 40 49C45 48 49 44 51 40 "
        "M53 12V25H40 M32 23V41M23 32H41",
        "M24 43V39C24 35 17 32 17 24C17 16 24 10 32 10C40 10 47 16 47 24C47 32 40 35 40 39V43Z "
        "M25 49H39M28 55H36 M32 1V4M10 8L14 12M1 25H7M54 12L58 8M57 25H63",
        "M18 22H41V45H18Z M25 17V22M34 17V22M25 45V50M34 45V50 M12 23C7 29 7 38 12 44 "
        "M47 23C52 29 52 38 47 44 M4 18C-4 27-4 40 4 49M55 18C63 27 63 40 55 49",
        "M24 25H39V42H24Z M29 20V25M34 20V25M29 42V47M34 42V47 "
        "M18 25C13 30 13 37 18 42 M45 25C50 30 50 37 45 42",
        "M25 43V39C25 35 19 32 19 25C19 18 25 13 32 13C39 13 45 18 45 25C45 32 39 35 39 39V43Z "
        "M26 49H38M29 55H35 M8 26C8 13 18 3 31 3C39 3 46 7 50 13M50 3V13H40",
        "M28 10H11V54H28M35 14L53 32L35 50M23 32H53",
    };
    C4fPath p = { 0 };
    c4fSvgPath(&p, icons[item], x, y, 1);
    c4fStrokePath(c, &p, 3, color);
    c4fPathFree(&p);
}

static const char *c4fToolHelp(int item, int available)
{
    static const char *const help[C4F_TESTER_ITEMS] = {
        "Return to live input", "Choose another player", "Recenter motion", "Choose any RGB color",
        "One-second vibration", "One-second vibration", "Restore default color", "Back to the launcher",
    };
    if (!available) return item == C4F_TESTER_USER ? "No other signed-in user" : "Controller unavailable";
    return help[item];
}

static void c4fPaintToolTile(C4fCanvas *c, int item, int focused, int available)
{
    static const char *const labels[C4F_TESTER_ITEMS] = {
        "Resume", "Switch user", "Reset orientation", "Light bar",
        "Large motor", "Small motor", "Reset light", "Leave test",
    };
    const C4fBox b = c4fToolBox(item);
    const C4fColor fill = focused ? (available ? C4F_RGBA(35, 91, 180, .94f) : C4F_RGBA(27, 57, 104, .96f)) :
                                  available ? C4F_RGBA(7, 29, 72, .82f) : C4F_RGBA(5, 20, 49, .82f);
    c4fFillRoundRect(c, b.x, b.y, b.w, b.h, 10, fill);
    c4fInsetRoundRect(c, b.x, b.y, b.w, b.h, 10, focused ? 3 : 1.5f,
                     focused ? C4F_ACCENT : available ? C4F_LINE : C4F_LINE_SOFT);
    C4fColor icon = available ? (focused ? C4F_TEXT : C4F_CROSS_KEY) : C4F_FAINT;
    c4fToolIcon(c, item, b.x + 28, b.y + 29, icon);
    c4fText(c, C4F_FONT_REGULAR, 30, b.x + 26, b.y + 137, available ? C4F_TEXT : C4F_FAINT, labels[item]);
    c4fText(c, C4F_FONT_LIGHT, 22, b.x + 26, b.y + 174, available ? C4F_MUTED : C4F_FAINT, c4fToolHelp(item, available));
    if (focused) {
        c4fFillRoundRect(c, b.x + 26, b.y + b.h - 16, b.w - 52, 3, 1.5f, C4F_ACCENT);
        c4fFillCircle(c, b.x + b.w - 28, b.y + 32, 4, C4F_TEXT);
    }
    if (!available) {
        /* A lock gives unavailable actions a second cue besides muted color. */
        const float x = b.x + b.w - 39, y = b.y + 54;
        c4fFillRoundRect(c, x, y, 17, 14, 3, C4F_FAINT);
        C4fPath p = { 0 };
        c4fSvgPath(&p, "M3 8V4C3-3 14-3 14 4V8", x, y - 7, 1);
        c4fStrokePath(c, &p, 2, C4F_FAINT);
        c4fPathFree(&p);
    }
}

static void c4fCopyOpaque(C4fCanvas *c, const C4fSprite *s)
{
    for (int y = 0; y < s->h; y++)
        memcpy(c->px + (s->y + y) * c->w + s->x, s->px + y * s->w, (size_t)s->w * sizeof(uint32_t));
}

static int c4fBuildTools(C4fCanvas *work)
{
    if (c4fToolsBase && c4fToolsFrame) return 1;
    const size_t bytes = (size_t)work->w * work->h * sizeof(uint32_t);
    if (!c4fToolsBase) {
        c4fToolsBase = c4fTakeCleanBackdrop();
        if (!c4fToolsBase) {
            c4fToolsBase = malloc(bytes);
            if (!c4fToolsBase) return 0;
            C4fCanvas background = { c4fToolsBase, work->w, work->h };
            c4fPaintBackdrop(&background, C4F_S);
        }
    }
    if (!c4fToolsBaseReady) {
        C4fCanvas c = { c4fToolsBase, work->w, work->h };
        c4fMark(&c, 88, 54, 48, C4F_WHITE);
        float x = 128;
        x += c4fText(&c, C4F_FONT_LIGHT, 17 * C4F_S, x, 63, C4F_TEXT, "Control4Free") + 14 * C4F_S;
        c4fDot(&c, x + 4 * C4F_S, 55, C4F_ACCENT, 0);
        c4fText(&c, C4F_FONT_LIGHT, 13 * C4F_S, x + 16 * C4F_S, 62, C4F_MUTED, "Controller test");
        c4fDrawSolidarityBadge(&c, C4F_SCREEN_WIDTH - 64, 40);
        c4fFillRect(&c, C4F_T_LEFT, 345, C4F_T_RIGHT - C4F_T_LEFT, 2, C4F_LINE);
        c4fFillRoundRect(&c, C4F_T_LEFT, 858, C4F_T_RIGHT - C4F_T_LEFT, 92, 10, C4F_RGBA(3, 16, 44, .5f));
        c4fText(&c, C4F_FONT_LIGHT, 20, C4F_T_LEFT, 1030, C4F_FAINT, "Control4Free " C4F_LAUNCHER_VERSION);
        c4fToolsBaseReady = 1;
    }
    if (!c4fToolsFrame) c4fToolsFrame = malloc(bytes);
    return c4fToolsFrame != NULL;
}

static void c4fDrawToolsDetails(C4fCanvas *c, const C4fTesterView *v, const C4fTesterConnection *connection)
{
    c4fText(c, C4F_FONT_LIGHT, 48, C4F_T_LEFT, 154, C4F_TEXT, v->colorMenu ? "Light bar" : "Controller tools");
    c4fText(c, C4F_FONT_LIGHT, 24, C4F_T_LEFT, 202, C4F_MUTED, v->colorMenu ?
            "Mix red, green and blue. Changes preview on the selected controller." : "Test motion, vibration and the light bar.");
    if (!v->colorMenu) c4fTextRight(c, C4F_FONT_LIGHT, 24, C4F_T_RIGHT, 202, C4F_MUTED, "D-pad or arrows to move");
    const C4fHint hints[2] = { { C4F_KEY_CROSS, v->colorMenu ? (v->colorFine ? "Fast adjustment" : "Fine adjustment") : "Select" },
                              { C4F_KEY_CIRCLE, v->colorMenu ? "Done" : "Back" } };
    c4fHints(c, C4F_T_RIGHT, 1022, hints, 2);
    c4fText(c, C4F_FONT_LIGHT, 23, C4F_T_LEFT, 991, C4F_MUTED, v->colorMenu ?
            "Up/Down: channel / Left/Right: adjust / OK: step size / Back: done" : "TV remote: OK select / Back return");
    const int selected = v->selected >= 0 && v->selected < C4F_TESTER_USERS;
    const int state = selected ? v->user[v->selected].state : -1;
    const char *name = selected ? v->user[v->selected].name : "No one signed in", *status;
    char detail[96];
    if (state == C4F_TESTER_READY) status = "Controller connected";
    else if (state == C4F_TESTER_NO_CONTROLLER) status = "No controller connected";
    else if (state == C4F_TESTER_SIGNED_OUT) status = "Signed out";
    else if (state == C4F_TESTER_FAILED) {
        snprintf(detail, sizeof(detail), "Cannot read (0x%08x)", (unsigned)v->user[v->selected].error);
        status = detail;
    } else status = selected ? "Opening controller..." : "Waiting for a signed-in user";
    c4fText(c, C4F_FONT_LIGHT, 20, C4F_T_LEFT, 257, C4F_FAINT, "SELECTED USER");
    c4fText(c, C4F_FONT_REGULAR, 30, C4F_T_LEFT, 300, C4F_TEXT, name);
    c4fText(c, C4F_FONT_LIGHT, 20, 620, 257, C4F_FAINT, "CONTROLLER");
    c4fDot(c, 629, 290, c4fStateColor(state), 0);
    c4fText(c, C4F_FONT_REGULAR, 26, 650, 300, state == C4F_TESTER_FAILED ? C4F_BAD : C4F_MUTED, status);
    c4fText(c, C4F_FONT_LIGHT, 20, 1240, 257, C4F_FAINT, "TEST OUTPUT");
    if (v->lightSet) {
        const C4fColor light = { v->light[0], v->light[1], v->light[2], 255 };
        c4fFillRoundRect(c, 1240, 278, 28, 18, 4, light);
        c4fText(c, C4F_FONT_LIGHT, 24, 1281, 294, C4F_MUTED, "Light bar set by this test");
    } else c4fText(c, C4F_FONT_LIGHT, 24, 1240, 294, C4F_MUTED, "Light bar: PS4 default");
    if (v->rumble) c4fText(c, C4F_FONT_REGULAR, 22, 1240, 322, C4F_WARN,
                           v->rumble == 1 ? "Large motor running" : "Small motor running");
    c4fDrawConnection(c, v, connection, C4F_T_LEFT, 329);
    const int focus = v->focus >= 0 && v->focus < C4F_TESTER_ITEMS ? v->focus : C4F_TESTER_RESUME;
    const int available = c4fTesterItemAvailable(v, focus);
    const char *note = v->note[0] ? v->note : v->colorMenu ? "The light bar returns to its PS4 color when you leave the tester." :
                       available ? "Choose a tool, or resume testing to see live input." :
                       focus == C4F_TESTER_USER ? "Sign in another PS4 user to switch players here." :
                       "Connect this user's controller to use this tool.";
    c4fTextWrapped(c, C4F_FONT_LIGHT, 24, C4F_T_LEFT + 26, 898, C4F_T_RIGHT - C4F_T_LEFT - 52, 31, 2,
                   v->note[0] ? C4F_TEXT : available ? C4F_MUTED : C4F_WARN, note);
}

static C4fSprite c4fColorRows[3][2], c4fColorPreview;

static void c4fRememberPickerPanel(C4fCanvas *c, C4fSprite *sprite, int x, int y, int w, int h)
{
    if (!c4fToolsBase) return;
    sprite->px = malloc((size_t)w * h * sizeof(uint32_t));
    if (!sprite->px) return;
    sprite->x = x; sprite->y = y; sprite->w = w; sprite->h = h;
    for (int row = 0; row < h; row++)
        memcpy(sprite->px + row * w, c->px + (y + row) * c->w + x, (size_t)w * sizeof(uint32_t));
}

/* Fill the opaque interior in straight spans; only rounded corners and the
 * thin edge need distance coverage. The color changes, the geometry does not. */
static void c4fPickerSwatch(C4fCanvas *c, C4fColor color)
{
    enum { X = 1324, Y = 430, W = 412, H = 210, R = 9 };
    c4fFillRect(c, X + R, Y, W - 2 * R, H, color);
    c4fFillRect(c, X, Y + R, R, H - 2 * R, color);
    c4fFillRect(c, X + W - R, Y + R, R, H - 2 * R, color);
    c4fFillCircle(c, X + R, Y + R, R, color);
    c4fFillCircle(c, X + W - R, Y + R, R, color);
    c4fFillCircle(c, X + R, Y + H - R, R, color);
    c4fFillCircle(c, X + W - R, Y + H - R, R, color);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (y >= R + 1 && y < H - R - 1 && x >= 2 && x < W - 2) continue;
            float d = c4fRoundBoxDistance(X + x + .5f, Y + y + .5f, X + W / 2.0f, Y + H / 2.0f, W / 2.0f, H / 2.0f, R);
            float outer = fmaxf(0, fminf(1, .5f - d)), inner = fmaxf(0, fminf(1, -.5f - d));
            if (outer > inner) c4fBlend(c, X + x, Y + y, C4F_LINE, outer - inner);
        }
    }
}

static void c4fDrawColorPicker(C4fCanvas *c, const C4fTesterView *v)
{
    static const char *const channels[] = { "Red", "Green", "Blue" };
    static const C4fColor ink[] = { C4F_RGBA(255, 103, 120, 1), C4F_RGBA(69, 224, 138, 1), C4F_RGBA(134, 182, 255, 1) };
    const int trackX = 195, trackW = 885;
    for (int channel = 0; channel < 3; channel++) {
        const int y = 360 + channel * 156, focused = channel == v->colorChannel;
        C4fSprite *row = &c4fColorRows[channel][focused];
        if (row->px) c4fCopyOpaque(c, row);
        else {
            c4fFillRoundRect(c, 150, y, 1110, 136, 10, focused ? C4F_RGBA(18, 58, 123, .96f) : C4F_RGBA(7, 29, 72, .82f));
            c4fInsetRoundRect(c, 150, y, 1110, 136, 10, focused ? 3 : 1, focused ? C4F_TEXT : C4F_LINE);
            c4fText(c, C4F_FONT_REGULAR, 28, 184, y + 43, ink[channel], channels[channel]);
            c4fRememberPickerPanel(c, row, 150, y, 1110, 136);
        }
        char value[16];
        snprintf(value, sizeof(value), "%u", v->light[channel]);
        c4fTextRight(c, C4F_FONT_REGULAR, 46, 1220, y + 88, C4F_TEXT, value);
        for (int x = 0; x < trackW; x++) {
            uint8_t rgb[3];
            memcpy(rgb, v->light, sizeof(rgb));
            rgb[channel] = (uint8_t)(x * 255 / (trackW - 1));
            c4fFillRect(c, trackX + x, y + 75, 1, 22, (C4fColor){ rgb[0], rgb[1], rgb[2], 255 });
        }
        c4fInsetRoundRect(c, trackX, y + 75, trackW, 22, 0, 1, C4F_LINE);
        const float knobX = trackX + v->light[channel] * (trackW - 1) / 255.0f;
        c4fFillCircle(c, knobX, y + 86, focused ? 14 : 11, C4F_TEXT);
        c4fFillCircle(c, knobX, y + 86, focused ? 9 : 7, ink[channel]);
    }
    if (c4fColorPreview.px) c4fCopyOpaque(c, &c4fColorPreview);
    else {
        c4fFillRoundRect(c, 1290, 360, 480, 448, 10, C4F_RGBA(3, 16, 44, .72f));
        c4fText(c, C4F_FONT_LIGHT, 21, 1324, 404, C4F_MUTED, "COLOR PREVIEW");
        c4fText(c, C4F_FONT_LIGHT, 22, 1324, 785, C4F_MUTED, "Cross / OK switches fine and fast");
        c4fRememberPickerPanel(c, &c4fColorPreview, 1290, 360, 480, 448);
    }
    c4fPickerSwatch(c, (C4fColor){ v->light[0], v->light[1], v->light[2], 255 });
    char value[32];
    snprintf(value, sizeof(value), "#%02X%02X%02X", v->light[0], v->light[1], v->light[2]);
    c4fText(c, C4F_FONT_REGULAR, 38, 1324, 701, C4F_TEXT, value);
    snprintf(value, sizeof(value), "Step: %d", v->colorFine ? 1 : 16);
    c4fText(c, C4F_FONT_REGULAR, 28, 1324, 753, C4F_TEXT, value);
}

static void c4fDrawMenu(C4fCanvas *c, const C4fTesterView *v, const C4fTesterConnection *connection)
{
    const size_t bytes = (size_t)c->w * c->h * sizeof(uint32_t);
    if (!c4fBuildTools(c)) {
        /* Keep the tools usable if caching could not allocate a frame. */
        if (c4fToolsBase) memcpy(c->px, c4fToolsBase, bytes);
        else for (int i = 0; i < c->w * c->h; i++) c->px[i] = 0xff061b42;
        if (v->colorMenu) c4fDrawColorPicker(c, v);
        else for (int i = 0; i < C4F_TESTER_ITEMS; i++) c4fPaintToolTile(c, i, i == v->focus, c4fTesterItemAvailable(v, i));
        c4fDrawToolsDetails(c, v, connection);
        return;
    }
    C4fTesterView key = { 0 };
    key.focus = v->focus; key.selected = v->selected; key.rumble = v->rumble; key.lightSet = v->lightSet;
    key.colorMenu = v->colorMenu; key.colorChannel = v->colorChannel; key.colorFine = v->colorFine;
    memcpy(key.light, v->light, sizeof(key.light));
    memcpy(key.note, v->note, sizeof(key.note));
    C4fTesterConnection connectionKey = { 0 };
    connectionKey.available = connection->available; connectionKey.pads = connection->pads;
    memcpy(connectionKey.rttMs, connection->rttMs, sizeof(connectionKey.rttMs));
    unsigned available = 0;
    for (int i = 0; i < C4F_TESTER_USERS; i++) {
        key.user[i].used = v->user[i].used; key.user[i].state = v->user[i].state; key.user[i].error = v->user[i].error;
        memcpy(key.user[i].name, v->user[i].name, sizeof(key.user[i].name));
    }
    for (int i = 0; i < C4F_TESTER_ITEMS; i++) if (c4fTesterItemAvailable(v, i)) available |= 1U << i;
    if (!c4fHasToolsKey || c4fToolsAvailable != available || memcmp(&key, &c4fToolsKey, sizeof(key)) ||
        memcmp(&connectionKey, &c4fToolsConnection, sizeof(connectionKey))) {
        C4fCanvas frame = { c4fToolsFrame, c->w, c->h };
        memcpy(frame.px, c4fToolsBase, bytes);
        if (v->colorMenu) c4fDrawColorPicker(&frame, v);
        else for (int i = 0; i < C4F_TESTER_ITEMS; i++) {
            const int on = i == v->focus, enabled = !!(available & (1U << i)), variant = on + 2 * !enabled;
            C4fSprite *tile = &c4fToolTiles[i][variant];
            if (!tile->px) {
                const C4fBox b = c4fToolBox(i);
                tile->x = (int)b.x; tile->y = (int)b.y; tile->w = C4F_TOOL_W; tile->h = C4F_TOOL_H;
                c4fPaintToolTile(&frame, i, on, enabled);
                tile->px = malloc((size_t)tile->w * tile->h * sizeof(uint32_t));
                if (tile->px) for (int y = 0; y < tile->h; y++)
                    memcpy(tile->px + y * tile->w, frame.px + (tile->y + y) * frame.w + tile->x,
                           (size_t)tile->w * sizeof(uint32_t));
            } else c4fCopyOpaque(&frame, tile);
        }
        c4fDrawToolsDetails(&frame, v, connection);
        c4fToolsKey = key; c4fToolsAvailable = available; c4fHasToolsKey = 1;
        c4fToolsConnection = connectionKey;
    }
    memcpy(c->px, c4fToolsFrame, bytes);
}

void c4fDrawTester(uint32_t *pixels, const C4fLauncherScreen *s)
{
    const C4fTesterView *v = &s->tester;
    C4fCanvas c = { pixels, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT };
    const size_t bytes = (size_t)c.w * (size_t)c.h * sizeof(uint32_t);
    if (v->menu) { c4fDrawMenu(&c, v, &s->connection); return; }
    const int state = v->selected >= 0 ? v->user[v->selected].state : -1, ready = state == C4F_TESTER_READY;
    const C4fColor accent = v->lightSet ? (C4fColor){ v->light[0], v->light[1], v->light[2], 255 } : C4F_ACCENT;
    if (!c4fLayer) c4fBuildLayer();
    c4fPrepareSprites(&c, v, accent, ready);
    if (c4fLayer) memcpy(pixels, c4fLayer, bytes);
    else memset(pixels, 0, bytes);

    c4fDrawTabs(&c, v);
    /* The page lights the controller in its colour; here, the colour this test set. */
    char line[96];
    const char *name = v->selected >= 0 ? v->user[v->selected].name : "", *detail;
    if (ready) {
        c4fDrawInput(&c, v, accent);
        detail = "Reading this user's controller";
    } else {
        /* Dim the picture so it does not read as a controller at rest. */
        c4fFillRoundRect(&c, C4F_P_X, C4F_P_Y, C4F_P_W, C4F_P_H, 12, C4F_RGBA(2, 6, 14, .55f));
        if (state == C4F_TESTER_NO_CONTROLLER) detail = "No controller connected";
        else if (state == C4F_TESTER_FAILED) {
            snprintf(line, sizeof(line), "The PS4 would not read it (0x%08x)", (unsigned)v->user[v->selected].error);
            detail = line;
        } else if (state == C4F_TESTER_SIGNED_OUT) detail = "Signed out";
        else if (state < 0) { name = "No one is signed in"; detail = NULL; }
        else detail = "Opening this user's controller...";
    }
    c4fDrawTouchpad(&c, accent, name, detail);
    c4fDrawValues(&c, v, ready);
    c4fDrawConnection(&c, v, &s->connection, C4F_P_X + 4, 853);
    if (v->rumble)
        c4fText(&c, C4F_FONT_REGULAR, 26, C4F_P_X + 4, C4F_T_TOP + C4F_T_HEIGHT - 30, C4F_WARN,
                v->rumble == 1 ? "Rumbling: large motor" : "Rumbling: small motor");
    if (v->note[0]) c4fText(&c, C4F_FONT_LIGHT, 24, C4F_T_LEFT, 972, C4F_TEXT, v->note);
    else c4fText(&c, C4F_FONT_LIGHT, 24, C4F_T_LEFT, 972, C4F_MUTED,
                 "Hold L3 + R3 for two seconds for the menu: change user, rumble, light bar, leave.");
}
