/* The launcher screen, drawn in the controller page's style (client/index.html):
 * the same blue backdrop with light waves, tiles, panels and colours, at 1.5x
 * the page's CSS sizes for a 1080p TV. */
#include "screen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "autorun.h"
#include "draw.h"
#include "logo.h"
#include "qrcodegen.h"

/* Page CSS pixels to TV pixels. */
#define C4F_S 1.5f

/* Layout, in TV pixels. */
#define C4F_LEFT         150.0f
#define C4F_RIGHT        1770.0f
#define C4F_ROW_TOP      336.0f
#define C4F_ROW_HEIGHT   444.0f
#define C4F_ROW_GAP      30.0f
#define C4F_QR_WIDTH     520.0f
#define C4F_MAIN_WIDTH   (C4F_RIGHT - C4F_LEFT - C4F_ROW_GAP - C4F_QR_WIDTH)
#define C4F_QR_LEFT      (C4F_LEFT + C4F_MAIN_WIDTH + C4F_ROW_GAP)
#define C4F_PANEL_TOP    (C4F_ROW_TOP + C4F_ROW_HEIGHT + 28)
#define C4F_PANEL_HEIGHT 128.0f

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

static uint32_t *c4fBackdrop;

static float c4fClamp(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* ---- backdrop (.backdrop, .waves, .sparkles) ---- */

static float c4fNoise(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) / 65536.0f - 0.5f;
}

static void c4fMix(float rgb[3], float r, float g, float b, float a)
{
    rgb[0] += (r - rgb[0]) * a;
    rgb[1] += (g - rgb[1]) * a;
    rgb[2] += (b - rgb[2]) * a;
}

static void c4fPaintGradient(C4fCanvas *c)
{
    static const float stops[4][4] = {
        { 0.00f, 0x02, 0x0f, 0x2b }, { 0.45f, 0x06, 0x27, 0x66 },
        { 0.72f, 0x0b, 0x3f, 0x9c }, { 1.00f, 0x04, 0x19, 0x4a },
    };
    const float w = (float)c->w, h = (float)c->h;
    /* linear-gradient(165deg): CSS angles point up at 0 and turn clockwise. */
    const float dx = 0.258819f, dy = 0.965926f, length = w * dx + h * dy;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float px = x + 0.5f, py = y + 0.5f, rgb[3];
            float t = c4fClamp(((px - w / 2) * dx + (py - h / 2) * dy) / length + 0.5f);
            int i = t > stops[2][0] ? 2 : t > stops[1][0] ? 1 : 0;
            float f = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);
            for (int k = 0; k < 3; k++) rgb[k] = stops[i][k + 1] + (stops[i + 1][k + 1] - stops[i][k + 1]) * f;
            /* radial-gradient(80% 60% at 0% -10%, rgba(25, 90, 200, .55), transparent 60%) */
            float ex = px / (0.8f * w), ey = (py + 0.1f * h) / (0.6f * h);
            c4fMix(rgb, 25, 90, 200, 0.55f * c4fClamp(1 - sqrtf(ex * ex + ey * ey) / 0.6f));
            /* radial-gradient(120% 70% at 75% 115%, rgba(70, 150, 255, .5), transparent 62%), on top */
            ex = (px - 0.75f * w) / (1.2f * w);
            ey = (py - 1.15f * h) / (0.7f * h);
            c4fMix(rgb, 70, 150, 255, 0.5f * c4fClamp(1 - sqrtf(ex * ex + ey * ey) / 0.62f));
            /* Dither, so the dark gradient does not band on a TV. */
            float noise = c4fNoise(x, y);
            uint32_t out = 0xff000000u;
            for (int k = 0; k < 3; k++) {
                int v = (int)floorf(rgb[k] + noise + 0.5f);
                out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << (16 - 8 * k);
            }
            c->px[(size_t)y * c->w + x] = out;
        }
}

/* The page's wave paths (viewBox 0 0 3200 400): the start y, then four cubic
 * segments (c1x c1y c2x c2y x y) with every "S" expanded. */
static const float c4fWaveA[25] = { 230, 200, 150, 400, 310, 800, 230, 1200, 150, 1400, 150, 1600, 230,
                                    1800, 150, 2000, 310, 2400, 230, 2800, 150, 3000, 150, 3200, 230 };
static const float c4fWaveB[25] = { 260, 250, 200, 550, 330, 800, 260, 1050, 190, 1350, 190, 1600, 260,
                                    1850, 200, 2150, 330, 2400, 260, 2650, 190, 2950, 190, 3200, 260 };
static const float c4fWaveC[25] = { 200, 300, 110, 500, 300, 800, 200, 1100, 100, 1300, 100, 1600, 200,
                                    1900, 110, 2100, 300, 2400, 200, 2700, 100, 2900, 100, 3200, 200 };
static const float c4fWaveD[25] = { 215, 300, 140, 520, 290, 800, 215, 1080, 140, 1320, 130, 1600, 215,
                                    1900, 140, 2120, 290, 2400, 215, 2680, 140, 2920, 130, 3200, 215 };
static const float c4fWaveE[25] = { 240, 280, 170, 540, 330, 800, 240, 1060, 150, 1330, 160, 1600, 240,
                                    1880, 170, 2140, 330, 2400, 240, 2660, 150, 2930, 160, 3200, 240 };

/* Gradient stops: offset, r, g, b, alpha. */
typedef float C4fWaveStops[3][5];
static const C4fWaveStops c4fSlowStops = { { 0, 255, 255, 255, 0 }, { .5f, 188, 216, 255, .55f }, { 1, 255, 255, 255, 0 } };
static const C4fWaveStops c4fMainStops = { { 0, 127, 178, 255, .1f }, { .5f, 255, 255, 255, .5f }, { 1, 127, 178, 255, .1f } };

static C4fColor c4fWaveColor(const C4fWaveStops stops, float t, float opacity)
{
    const float *a = stops[0], *b = stops[1];
    t = c4fClamp(t);
    if (t > stops[1][0]) { a = stops[1]; b = stops[2]; }
    float f = (t - a[0]) / (b[0] - a[0]);
    /* Interpolate premultiplied, as browsers do. */
    float alpha = a[4] + (b[4] - a[4]) * f, rgb[3];
    for (int k = 0; k < 3; k++) {
        float v = a[k + 1] * a[4] + (b[k + 1] * b[4] - a[k + 1] * a[4]) * f;
        rgb[k] = alpha > 0 ? v / alpha : a[k + 1];
    }
    return (C4fColor){ (uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2], (uint8_t)(alpha * opacity * 255 + 0.5f) };
}

static void c4fPaintWave(C4fCanvas *c, const float path[25], float offset, float top, float sx, float sy,
                         float stroke, float opacity, const C4fWaveStops stops)
{
    enum { STEPS = 48, POINTS = 4 * STEPS + 1 };
    float px[POINTS], py[POINTS], x0 = 0, y0 = path[0];
    int n = 1;
    px[0] = offset;
    py[0] = top + y0 * sy;
    for (int s = 0; s < 4; s++) {
        const float *k = path + 1 + s * 6;
        for (int i = 1; i <= STEPS; i++) {
            float t = (float)i / STEPS, u = 1 - t;
            float bx = u * u * u * x0 + 3 * u * u * t * k[0] + 3 * u * t * t * k[2] + t * t * t * k[4];
            float by = u * u * u * y0 + 3 * u * u * t * k[1] + 3 * u * t * t * k[3] + t * t * t * k[5];
            px[n] = offset + bx * sx;
            py[n] = top + by * sy;
            n++;
        }
        x0 = k[4];
        y0 = k[5];
    }
    /* The paths only ever move right, so each column crosses a path once. */
    float half = fmaxf(stroke * sy / 2, 0.35f);
    int seg = 0;
    for (int x = 0; x < c->w; x++) {
        float fx = x + 0.5f;
        if (fx < px[0] || fx > px[n - 1]) continue;
        while (seg < n - 2 && px[seg + 1] < fx) seg++;
        float span = px[seg + 1] - px[seg];
        float slope = span > 0 ? (py[seg + 1] - py[seg]) / span : 0;
        float yc = py[seg] + slope * (fx - px[seg]), norm = sqrtf(1 + slope * slope);
        C4fColor color = c4fWaveColor(stops, (fx - offset) / (sx * 3200), opacity);
        for (int y = (int)floorf(yc - (half + 1) * norm); y <= (int)ceilf(yc + (half + 1) * norm); y++) {
            float cov = c4fClamp(half + 0.5f - fabsf(y + 0.5f - yc) / norm);
            if (cov > 0) c4fBlend(c, x, y, color, cov);
        }
    }
}

static void c4fPaintBackdrop(C4fCanvas *c, float scale)
{
    const float w = (float)c->w, h = (float)c->h, sx = 2 * w / 3200;
    c4fPaintGradient(c);
    /* A still from the drifting animation, the brightest part mid-screen.
     * .waves.slow: bottom 14%, height 50%, opacity .7. */
    float top = 0.36f * h, sy = 0.5f * h / 400, offset = -0.70f * w;
    c4fPaintWave(c, c4fWaveA, offset, top, sx, sy, 1.2f, 0.7f, c4fSlowStops);
    c4fPaintWave(c, c4fWaveB, offset, top, sx, sy, 2.0f, 0.7f * 0.6f, c4fSlowStops);
    /* .waves: bottom 4%, height 62%. */
    top = 0.34f * h;
    sy = 0.62f * h / 400;
    offset = -0.38f * w;
    c4fPaintWave(c, c4fWaveC, offset, top, sx, sy, 1.5f, 1.0f, c4fMainStops);
    c4fPaintWave(c, c4fWaveD, offset, top, sx, sy, 1.0f, 0.7f, c4fMainStops);
    c4fPaintWave(c, c4fWaveE, offset, top, sx, sy, 3.0f, 0.35f, c4fMainStops);
    /* .sparkles spans -10% to 110% of the height. */
    static const float dots[8][4] = {
        { .12f, .22f, 1.5f, .70f }, { .28f, .64f, 1.0f, .55f }, { .47f, .38f, 1.5f, .60f }, { .63f, .76f, 1.0f, .50f },
        { .78f, .30f, 2.0f, .45f }, { .88f, .58f, 1.0f, .60f }, { .36f, .88f, 1.5f, .50f }, { .92f, .12f, 1.0f, .55f },
    };
    for (int i = 0; i < 8; i++) {
        float cx = dots[i][0] * w, cy = -0.1f * h + dots[i][1] * 1.2f * h, r = fmaxf(dots[i][2] * scale, 1.5f);
        for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++)
            for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
                float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
                if (d < r) c4fBlend(c, x, y, C4F_RGBA(255, 255, 255, dots[i][3]), 1 - d / r);
            }
    }
}

static int c4fClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* One box-blur pass along rows or columns of an RGB buffer, edges clamped. */
static void c4fBoxPass(const uint8_t *src, uint8_t *dst, int w, int h, int radius, int horizontal)
{
    const int n = horizontal ? w : h, lines = horizontal ? h : w, divisor = 2 * radius + 1;
    const int step = horizontal ? 3 : w * 3, lineStep = horizontal ? w * 3 : 3;
    for (int line = 0; line < lines; line++) {
        const uint8_t *s = src + (size_t)line * lineStep;
        uint8_t *d = dst + (size_t)line * lineStep;
        for (int k = 0; k < 3; k++) {
            int sum = 0;
            for (int i = -radius; i <= radius; i++) sum += s[c4fClampI(i, 0, n - 1) * step + k];
            for (int i = 0; i < n; i++) {
                d[i * step + k] = (uint8_t)((sum + divisor / 2) / divisor);
                sum += s[c4fClampI(i + radius + 1, 0, n - 1) * step + k] - s[c4fClampI(i - radius, 0, n - 1) * step + k];
            }
        }
    }
}

/* backdrop-filter: blur(sigma) under a tile. The backdrop never changes, so
 * this is done once, clipped to the tile's rounded shape. */
static void c4fBlurUnder(C4fCanvas *c, float x, float y, float w, float h, float r, float sigma)
{
    /* Three box passes of this radius approximate a Gaussian of that sigma. */
    const int radius = (int)((sqrtf(4 * sigma * sigma + 1) - 1) / 2 + 0.5f), margin = 3 * radius;
    const int x0 = c4fClampI((int)floorf(x) - margin, 0, c->w), y0 = c4fClampI((int)floorf(y) - margin, 0, c->h);
    const int x1 = c4fClampI((int)ceilf(x + w) + margin, 0, c->w), y1 = c4fClampI((int)ceilf(y + h) + margin, 0, c->h);
    const int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0 || radius < 1) return;
    uint8_t *buffer = malloc((size_t)bw * bh * 3), *scratch = malloc((size_t)bw * bh * 3);
    if (buffer && scratch) {
        for (int py = 0; py < bh; py++)
            for (int px = 0; px < bw; px++) {
                uint32_t v = c->px[(size_t)(y0 + py) * c->w + x0 + px];
                uint8_t *o = buffer + ((size_t)py * bw + px) * 3;
                o[0] = (uint8_t)(v >> 16); o[1] = (uint8_t)(v >> 8); o[2] = (uint8_t)v;
            }
        for (int pass = 0; pass < 3; pass++) {
            c4fBoxPass(buffer, scratch, bw, bh, radius, 1);
            c4fBoxPass(scratch, buffer, bw, bh, radius, 0);
        }
        const float cx = x + w / 2, cy = y + h / 2;
        for (int py = 0; py < bh; py++)
            for (int px = 0; px < bw; px++) {
                int gx = x0 + px, gy = y0 + py;
                float cov = c4fClamp(0.5f - c4fRoundBoxDistance(gx + 0.5f, gy + 0.5f, cx, cy, w / 2, h / 2, r));
                if (cov <= 0) continue;
                uint32_t *p = &c->px[(size_t)gy * c->w + gx], out = 0xff000000u;
                const uint8_t *b = buffer + ((size_t)py * bw + px) * 3;
                float noise = c4fNoise(gx, gy);
                for (int k = 0; k < 3; k++) {
                    float old = (float)((*p >> (16 - 8 * k)) & 255u);
                    int v = (int)floorf(old + (b[k] - old) * cov + noise + 0.5f);
                    out |= (uint32_t)c4fClampI(v, 0, 255) << (16 - 8 * k);
                }
                *p = out;
            }
    }
    free(buffer);
    free(scratch);
}

/* ---- pieces of the page ---- */

/* The DualShock 4 mark, its cut-outs showing what is behind. */
static void c4fMark(C4fCanvas *c, float x, float y, float width, C4fColor color)
{
    C4fPath path = { 0 };
    c4fLogoPath(&path, 1);
    c4fFillPath(c, &path, c4fLogoAt(x, y, width, 0), color);
    c4fPathFree(&path);
}

/* A DualShock 4 with its light bar lit: the bar glows over the top edge, and
 * the cut-outs read as dark recesses. */
static void c4fLitController(C4fCanvas *c, float x, float y, float width, float angle,
                             C4fColor body, C4fColor light, int shadow)
{
    C4fPath outline = { 0 }, detail = { 0 }, bar = { 0 };
    C4fPlacement at = c4fLogoAt(x, y, width, angle);
    const float k = width / C4F_LOGO_WIDTH;
    c4fLogoPath(&outline, 0);
    c4fLogoPath(&detail, 1);
    c4fLogoLightPath(&bar);
    if (shadow) c4fShadowPath(c, &outline, at, 0, 26 * k, (int)fmaxf(2, 22 * k), C4F_RGBA(0, 4, 18, .75f));
    /* The light spills up and out over the top edge, like the real bar: a
     * wide soft source behind the edge, and a tight one at the bar. */
    C4fPath spill = { 0 };
    c4fPathRoundRect(&spill, 330, 46, 340, 56, 28, 0);
    c4fShadowPath(c, &spill, at, 0, -6 * k, (int)fmaxf(3, 42 * k), light);
    c4fShadowPath(c, &bar, at, 0, -8 * k, (int)fmaxf(2, 22 * k), light);
    c4fPathFree(&spill);
    c4fFillPath(c, &outline, at, C4F_RGBA(6, 22, 62, 1));
    c4fFillPath(c, &detail, at, body);
    c4fFillPath(c, &bar, at, light);
    /* A bright core makes it read as a light, not paint. */
    C4fPath core = { 0 };
    c4fPathRoundRect(&core, 370, 83, 260, 7, 3.5f, 0);
    c4fFillPath(c, &core, at, C4F_RGBA((light.r + 255) / 2, (light.g + 255) / 2, (light.b + 255) / 2, .9f));
    c4fPathFree(&core);
    c4fPathFree(&outline);
    c4fPathFree(&detail);
    c4fPathFree(&bar);
}

static void c4fTextCentered(C4fCanvas *c, int font, float size, float cx, float y, C4fColor color, const char *text)
{
    c4fText(c, font, size, cx - c4fTextWidth(font, size, text) / 2, y, color, text);
}

/* .tile: --tile with a light sheen, a 1px line, a drop shadow and the colour bar. */
static void c4fTile(C4fCanvas *c, float x, float y, float w, float h, C4fColor bar)
{
    const float r = 6 * C4F_S;
    c4fShadowRoundRect(c, x, y, w, h, r, 0, 16 * C4F_S, 34 * C4F_S, -18 * C4F_S, C4F_RGBA(0, 0, 0, .9f));
    c4fFillRoundRect(c, x, y, w, h, r, C4F_TILE);
    c4fSheenRoundRect(c, x, y, w, h, r, 160, C4F_WHITE, .13f, .02f, .55f);
    c4fInsetRoundRect(c, x, y, w, h, r, 1 * C4F_S, C4F_LINE);
    bar.a = (uint8_t)(bar.a * .9f);
    c4fBandRoundRect(c, x, y, w, h, r, 3 * C4F_S, bar);
}

/* .panel */
static void c4fPanel(C4fCanvas *c, float x, float y, float w, float h, C4fColor fill, C4fColor line)
{
    c4fFillRoundRect(c, x, y, w, h, 6 * C4F_S, fill);
    c4fInsetRoundRect(c, x, y, w, h, 6 * C4F_S, 1 * C4F_S, line);
}

/* .ring: the numbered circle on a controller tile. */
static void c4fRingBadge(C4fCanvas *c, float cx, float cy, C4fColor color, const char *label)
{
    const float r = 26 * C4F_S, hx = cx, hy = cy - 0.3f * r, far = hypotf(r, 1.3f * r);
    c4fGlowCircle(c, cx, cy, r, 18 * C4F_S, -4 * C4F_S, color);
    /* radial-gradient(circle at 50% 35%, rgba(255, 255, 255, .18), rgba(255, 255, 255, .03)) */
    for (int y = (int)floorf(cy - r - 1); y <= (int)ceilf(cy + r + 1); y++)
        for (int x = (int)floorf(cx - r - 1); x <= (int)ceilf(cx + r + 1); x++) {
            float inside = c4fClamp(0.5f - (hypotf(x + 0.5f - cx, y + 0.5f - cy) - r));
            if (inside <= 0) continue;
            float t = c4fClamp(hypotf(x + 0.5f - hx, y + 0.5f - hy) / far);
            c4fBlend(c, x, y, C4F_RGBA(255, 255, 255, .18f + (.03f - .18f) * t), inside);
        }
    c4fRing(c, cx, cy, r, 3 * C4F_S, color);
    c4fTextCentered(c, C4F_FONT_LIGHT, 24 * C4F_S, cx, cy + 24 * C4F_S * 0.36f, C4F_TEXT, label);
}

/* .dot, with the glow the page gives a live connection. */
static void c4fDot(C4fCanvas *c, float cx, float cy, C4fColor color, int glow)
{
    if (glow) c4fGlowCircle(c, cx, cy, 4 * C4F_S, 10 * C4F_S, 0, color);
    c4fFillCircle(c, cx, cy, 4 * C4F_S, color);
}

/* Button guide in the PS4's manner, with the page's face-button colours. */
enum { C4F_KEY_CROSS, C4F_KEY_CIRCLE, C4F_KEY_SQUARE, C4F_KEY_TRIANGLE };
typedef struct { int key; const char *label; } C4fHint;
#define C4F_HINT_RADIUS 22.0f
#define C4F_HINT_GAP 14.0f
#define C4F_HINT_SPACING 40.0f
#define C4F_HINT_SIZE 26.0f

static void c4fHints(C4fCanvas *c, float right, float cy, const C4fHint *hints, int count)
{
    float x = right;
    for (int i = 0; i < count; i++)
        x -= (i ? C4F_HINT_SPACING : 0) + 2 * C4F_HINT_RADIUS + C4F_HINT_GAP +
             c4fTextWidth(C4F_FONT_REGULAR, C4F_HINT_SIZE, hints[i].label);
    for (int i = 0; i < count; i++) {
        float cx = x + C4F_HINT_RADIUS;
        c4fFillCircle(c, cx, cy, C4F_HINT_RADIUS, C4F_RGBA(255, 255, 255, .08f));
        c4fRing(c, cx, cy, C4F_HINT_RADIUS, 1 * C4F_S, C4F_LINE_SOFT);
        if (hints[i].key == C4F_KEY_CROSS) {
            c4fSegment(c, cx - 8.5f, cy - 8.5f, cx + 8.5f, cy + 8.5f, 3.6f, C4F_CROSS_KEY);
            c4fSegment(c, cx - 8.5f, cy + 8.5f, cx + 8.5f, cy - 8.5f, 3.6f, C4F_CROSS_KEY);
        } else if (hints[i].key == C4F_KEY_CIRCLE) {
            c4fRing(c, cx, cy, 10.5f, 3.6f, C4F_CIRCLE_KEY);
        } else if (hints[i].key == C4F_KEY_TRIANGLE) {
            c4fTriangleOutline(c, cx, cy + 1.5f, 11.5f, 3.4f, C4F_TRIANGLE_KEY);
        } else {
            c4fSquareOutline(c, cx, cy, 8.5f, 3.4f, C4F_SQUARE_KEY);
        }
        x += 2 * C4F_HINT_RADIUS + C4F_HINT_GAP;
        x += c4fText(c, C4F_FONT_REGULAR, C4F_HINT_SIZE, x, cy + C4F_HINT_SIZE * 0.36f, C4F_TEXT, hints[i].label);
        x += C4F_HINT_SPACING;
    }
}

static void c4fQrCard(C4fCanvas *c, float x, float y, float size, const char *address)
{
    static char lastAddress[64];
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    static int qrSize;
    if (strcmp(lastAddress, address)) {
        uint8_t temp[sizeof(qr)];
        qrSize = qrcodegen_encodeText(address, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 5, qrcodegen_Mask_AUTO, true)
            ? qrcodegen_getSize(qr) : 0;
        snprintf(lastAddress, sizeof(lastAddress), "%s", address);
    }
    c4fFillRoundRect(c, x, y, size, size, 6 * C4F_S, C4F_WHITE);
    if (!qrSize) return;
    /* Whole pixels per module and a four-module quiet zone keep it scannable. */
    int scale = (int)(size / (float)(qrSize + 8)), drawn = qrSize * scale;
    int left = (int)(x + (size - drawn) / 2), top = (int)(y + (size - drawn) / 2);
    for (int row = 0; row < qrSize; row++)
        for (int col = 0; col < qrSize; col++)
            if (qrcodegen_getModule(qr, col, row))
                c4fFillRect(c, left + col * scale, top + row * scale, scale, scale, C4F_RGBA(4, 14, 40, 1));
}

/* ---- the screen ---- */

typedef struct {
    const char *pill, *title, *subtitle, *meta;
    C4fColor dot, bar;
    int glow;
} C4fLook;

static C4fLook c4fLookFor(const C4fLauncherScreen *s, char *meta, size_t metaSize)
{
    C4fLook look = { "Not responding", "Check Control4Free",
                     "Something answers on port 4264 but not the way this app expects. If an older Control4Free "
                     "is running, stop it on its controller page or restart the PS4.",
                     "Not responding", C4F_BAD, C4F_BAD, 0 };
    if (s->locked) {
        look.pill = look.meta = "Restart needed";
        look.title = "Restart the PS4";
        look.subtitle = "Control4Free was sent but never answered. Restart the console before you try again, so "
                        "two copies never run at once.";
    } else if (s->busy) {
        look = (C4fLook){ "Working", "Please wait", s->message, "Checking", C4F_WARN, C4F_ACCENT, 0 };
    } else if (s->running == 1) {
        if (s->controllers == 1) snprintf(meta, metaSize, "1 controller connected");
        else snprintf(meta, metaSize, "%d controllers connected", s->controllers);
        look = (C4fLook){ "Running", "Control4Free is running",
                          "Open the address or scan the code on your phone or PC. Pick a controller there, then "
                          "choose its user on the PS4.",
                          meta, C4F_OK, C4F_ACCENT, 1 };
    } else if (s->running == 0 && s->autorun == C4F_AUTORUN_ON) {
        look = (C4fLook){ "Stopped", "Start Control4Free",
                          "It starts with GoldHEN after each restart. To start it now, turn on GoldHEN's PayLoader "
                          "and press Cross.",
                          "Not running", C4F_FAINT, C4F_FAINT, 0 };
    } else if (s->running == 0) {
        look = (C4fLook){ "Stopped", "Set up Control4Free",
                          "Press Cross once. GoldHEN will start Control4Free each time it loads, and it starts "
                          "right away if GoldHEN's PayLoader is on.",
                          "Not running", C4F_FAINT, C4F_FAINT, 0 };
    }
    return look;
}

void c4fDrawLauncher(uint32_t *pixels, const C4fLauncherScreen *s)
{
    C4fCanvas c = { pixels, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT };
    const float left = C4F_LEFT, right = C4F_RIGHT, top = C4F_ROW_TOP, height = C4F_ROW_HEIGHT;
    const float qrWidth = C4F_QR_WIDTH, mainWidth = C4F_MAIN_WIDTH, pad = 16 * C4F_S;
    char meta[48], count[8];
    C4fLook look = c4fLookFor(s, meta, sizeof(meta));
    int live = s->running == 1 && !s->locked;

    if (c4fBackdrop) memcpy(pixels, c4fBackdrop, (size_t)c.w * (size_t)c.h * sizeof(uint32_t));
    else memset(pixels, 0, (size_t)c.w * (size_t)c.h * sizeof(uint32_t));

    /* .topbar: the app mark, the name and the state. */
    c4fMark(&c, 64 + 24, 54, 48, C4F_WHITE);
    float x = 64 + 48 + 16;
    x += c4fText(&c, C4F_FONT_LIGHT, 17 * C4F_S, x, 63, C4F_TEXT, "Control4Free") + 14 * C4F_S;
    c4fDot(&c, x + 4 * C4F_S, 55, look.dot, look.glow);
    c4fText(&c, C4F_FONT_LIGHT, 13 * C4F_S, x + 16 * C4F_S, 62, C4F_MUTED, look.pill);

    /* .screen-title and .screen-sub */
    c4fText(&c, C4F_FONT_LIGHT, 40 * C4F_S, left, 196, C4F_TEXT, look.title);
    c4fTextWrapped(&c, C4F_FONT_LIGHT, 26, left, 250, 1500, 39, 2, C4F_MUTED, look.subtitle);

    /* Where to connect, as a controller tile. */
    c4fTile(&c, left, top, mainWidth, height, look.bar);
    if (s->running == 1) snprintf(count, sizeof(count), "%d", s->controllers);
    else snprintf(count, sizeof(count), "-");
    c4fRingBadge(&c, left + pad + 26 * C4F_S, top + pad + 26 * C4F_S + 4, look.bar, count);
    float textX = left + pad + 52 * C4F_S + 22;
    c4fText(&c, C4F_FONT_REGULAR, 30, textX, top + 66, C4F_TEXT, "Open on your phone or PC");
    c4fText(&c, C4F_FONT_LIGHT, 23, textX, top + 102, C4F_MUTED, look.meta);

    float fieldX = left + pad, fieldY = top + 150, fieldWidth = mainWidth - 2 * pad, fieldHeight = 88;
    c4fFillRoundRect(&c, fieldX, fieldY, fieldWidth, fieldHeight, 4 * C4F_S, C4F_RGBA(0, 0, 0, .3f));
    c4fInsetRoundRect(&c, fieldX, fieldY, fieldWidth, fieldHeight, 4 * C4F_S, 1 * C4F_S, C4F_LINE);
    if (s->address[0])
        c4fText(&c, C4F_FONT_REGULAR, 40, fieldX + 28, fieldY + fieldHeight / 2 + 14, live ? C4F_TEXT : C4F_MUTED, s->address);
    else
        c4fText(&c, C4F_FONT_LIGHT, 28, fieldX + 28, fieldY + fieldHeight / 2 + 10, C4F_MUTED,
                "Connect the PS4 to your home network");
    c4fTextWrapped(&c, C4F_FONT_LIGHT, 24, fieldX, fieldY + fieldHeight + 44, fieldWidth, 36, 2, C4F_MUTED,
                   "Use the same network as the PS4. Touch, a keyboard and controllers connected to the phone "
                   "or PC all work.");

    /* The last result along the bottom of the tile. */
    float lineY = top + height - 104;
    c4fFillRoundRect(&c, fieldX, lineY, fieldWidth, 1 * C4F_S, 0, C4F_LINE_SOFT);
    c4fDot(&c, fieldX + 4 * C4F_S, lineY + 44, look.dot, look.glow);
    c4fTextWrapped(&c, C4F_FONT_LIGHT, 24, fieldX + 24, lineY + 52, fieldWidth - 24, 34, 2, C4F_TEXT,
                   s->busy ? "Working..." : s->message);

    /* The QR code tile. */
    float qrX = C4F_QR_LEFT, card = 340, cardX = qrX + (qrWidth - card) / 2, cardY = top + 34;
    c4fTile(&c, qrX, top, qrWidth, height, look.bar);
    if (live && s->address[0]) {
        c4fQrCard(&c, cardX, cardY, card, s->address);
        c4fTextCentered(&c, C4F_FONT_LIGHT, 24, qrX + qrWidth / 2, cardY + card + 50, C4F_MUTED,
                        "Scan with your phone's camera");
    } else {
        c4fFillRoundRect(&c, cardX, cardY, card, card, 6 * C4F_S, C4F_RGBA(0, 0, 0, .3f));
        c4fInsetRoundRect(&c, cardX, cardY, card, card, 6 * C4F_S, 1 * C4F_S, C4F_LINE_SOFT);
        c4fMark(&c, cardX + card / 2, cardY + card / 2, 180, C4F_RGBA(226, 236, 255, .3f));
        c4fTextCentered(&c, C4F_FONT_LIGHT, 24, qrX + qrWidth / 2, cardY + card + 50, C4F_MUTED,
                        "The code appears once it runs");
    }

    /* .panel: whether GoldHEN starts it by itself. */
    const char *panelTitle, *panelText;
    C4fColor panelDot = C4F_FAINT;
    if (s->autorun == C4F_AUTORUN_ON) {
        panelTitle = "Starts by itself with GoldHEN";
        panelText = "GoldHEN starts Control4Free each time it loads. Closing this app leaves your controllers "
                    "running. Turn off other controller plugins in your games.";
        panelDot = C4F_OK;
    } else if (s->autorun == C4F_AUTORUN_OUTDATED) {
        panelTitle = "Auto-start uses an older Control4Free";
        panelText = "Press Triangle to switch GoldHEN's auto-start to this version. It takes effect after the "
                    "next restart.";
        panelDot = C4F_WARN;
    } else if (s->autorun == C4F_AUTORUN_OFF) {
        panelTitle = "Auto-start is off";
        panelText = "Press Triangle and GoldHEN starts Control4Free each time it loads, with no PC needed.";
    } else {
        panelTitle = "Auto-start status unknown";
        panelText = s->autorunNote[0] ? s->autorunNote : "Checking with GoldHEN...";
        panelDot = s->autorunNote[0] ? C4F_BAD : C4F_WARN;
    }
    float panelY = C4F_PANEL_TOP;
    c4fPanel(&c, left, panelY, right - left, C4F_PANEL_HEIGHT, C4F_PANEL, C4F_LINE_SOFT);
    c4fDot(&c, left + 20 * C4F_S + 4 * C4F_S, panelY + 43, panelDot, s->autorun == C4F_AUTORUN_ON);
    c4fText(&c, C4F_FONT_LIGHT, 19 * C4F_S, left + 20 * C4F_S + 24, panelY + 52, C4F_TEXT, panelTitle);
    c4fTextWrapped(&c, C4F_FONT_LIGHT, 23, left + 20 * C4F_S, panelY + 94, right - left - 40 * C4F_S, 34, 1, C4F_MUTED,
                   panelText);

    /* .home-foot and the button guide. */
    c4fText(&c, C4F_FONT_LIGHT, 20, left, 1030, C4F_FAINT, "Control4Free " C4F_LAUNCHER_VERSION);
    if (!s->confirmStop && !s->busy) {
        C4fHint hints[4];
        int n = 0;
        if (!s->locked && !live) {
            hints[n].key = C4F_KEY_CROSS;
            hints[n++].label = s->autorun == C4F_AUTORUN_ON ? "Start" : "Set up";
        }
        if (s->autorun != C4F_AUTORUN_UNKNOWN) {
            hints[n].key = C4F_KEY_TRIANGLE;
            hints[n++].label = s->autorun == C4F_AUTORUN_ON ? "Turn off auto-start" :
                               s->autorun == C4F_AUTORUN_OUTDATED ? "Update auto-start" : "Turn on auto-start";
        }
        if (live) {
            hints[n].key = C4F_KEY_SQUARE;
            hints[n++].label = "Stop";
        }
        hints[n].key = C4F_KEY_CIRCLE;
        hints[n++].label = "Close";
        c4fHints(&c, right, 1022, hints, n);
    }

    if (s->confirmStop) {
        const float dw = 980, dh = 330, dx = (c.w - dw) / 2, dy = (c.h - dh) / 2;
        static const C4fHint hints[2] = { { C4F_KEY_CROSS, "Stop" }, { C4F_KEY_CIRCLE, "Cancel" } };
        c4fFillRect(&c, 0, 0, c.w, c.h, C4F_RGBA(2, 11, 31, .6f));
        c4fShadowRoundRect(&c, dx, dy, dw, dh, 6 * C4F_S, 0, 16 * C4F_S, 34 * C4F_S, -18 * C4F_S, C4F_RGBA(0, 0, 0, .9f));
        /* A little more opaque than --panel-strong, so the tiles do not ghost through. */
        c4fPanel(&c, dx, dy, dw, dh, C4F_RGBA(2, 11, 31, .94f), C4F_LINE);
        c4fText(&c, C4F_FONT_LIGHT, 48, dx + 48, dy + 96, C4F_TEXT, "Stop Control4Free?");
        c4fTextWrapped(&c, C4F_FONT_LIGHT, 26, dx + 48, dy + 152, dw - 96, 39, 2, C4F_MUTED,
                       "Every controller disconnects. You can start Control4Free again from this screen.");
        c4fHints(&c, dx + dw - 48, dy + dh - 58, hints, 2);
    }
}

void c4fDrawIcon(uint32_t *pixels, int size)
{
    C4fCanvas c = { pixels, size, size };
    const float s = (float)size;
    c4fPaintBackdrop(&c, s / 360.0f);
    /* Three players, their light bars in the page's player colours. */
    c4fLitController(&c, s * 0.31f, s * 0.385f, s * 0.50f, -15, C4F_RGBA(196, 210, 240, 1), C4F_RGBA(255, 48, 64, 1), 0);
    c4fLitController(&c, s * 0.69f, s * 0.385f, s * 0.50f, 15, C4F_RGBA(196, 210, 240, 1), C4F_RGBA(48, 200, 96, 1), 0);
    c4fLitController(&c, s * 0.50f, s * 0.50f, s * 0.62f, 0, C4F_WHITE, C4F_RGBA(58, 140, 255, 1), 1);
    c4fTextCentered(&c, C4F_FONT_LIGHT, s * 0.1f, s / 2.0f, s * 0.89f, C4F_TEXT, "Control4Free");
}

int c4fScreenInit(void)
{
    if (c4fDrawInit()) return -1;
    if (!c4fBackdrop) {
        c4fBackdrop = malloc((size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT * sizeof(uint32_t));
        if (!c4fBackdrop) return -1;
        C4fCanvas c = { c4fBackdrop, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT };
        c4fPaintBackdrop(&c, C4F_S);
        /* .tile has backdrop-filter: blur(6px), .panel blur(10px). */
        c4fBlurUnder(&c, C4F_LEFT, C4F_ROW_TOP, C4F_MAIN_WIDTH, C4F_ROW_HEIGHT, 6 * C4F_S, 6 * C4F_S);
        c4fBlurUnder(&c, C4F_QR_LEFT, C4F_ROW_TOP, C4F_QR_WIDTH, C4F_ROW_HEIGHT, 6 * C4F_S, 6 * C4F_S);
        c4fBlurUnder(&c, C4F_LEFT, C4F_PANEL_TOP, C4F_RIGHT - C4F_LEFT, C4F_PANEL_HEIGHT, 6 * C4F_S, 10 * C4F_S);
    }
    return 0;
}
