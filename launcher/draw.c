/* Software drawing for the native launcher. Shape coverage comes from signed
 * distances, text from stb_truetype; everything is blended in 8-bit ARGB. */
#include "draw.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "stb_truetype.h"

extern const unsigned char c4fFontLight[], c4fFontRegular[];

typedef struct { unsigned char *bitmap; int w, h, x0, y0, ready; float advance; } C4fGlyph;
typedef struct { int font, size; float scale; C4fGlyph glyph[95]; } C4fFace;

static stbtt_fontinfo c4fFonts[C4F_FONT_COUNT];
static C4fFace c4fFaces[24];
static int c4fFaceCount;
static uint8_t c4fTextGamma[256];

int c4fDrawInit(void)
{
    const unsigned char *data[C4F_FONT_COUNT] = { c4fFontLight, c4fFontRegular };
    for (int i = 0; i < C4F_FONT_COUNT; i++)
        if (!stbtt_InitFont(&c4fFonts[i], data[i], stbtt_GetFontOffsetForIndex(data[i], 0))) return -1;
    /* Light strokes on a dark background read thinner than a browser draws
     * them; lift partial coverage a little. */
    for (int i = 0; i < 256; i++) c4fTextGamma[i] = (uint8_t)(powf(i / 255.0f, 0.8f) * 255.0f + 0.5f);
    return 0;
}

static float c4fClamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int c4fMinI(int a, int b) { return a < b ? a : b; }
static int c4fMaxI(int a, int b) { return a > b ? a : b; }

void c4fBlend(C4fCanvas *c, int x, int y, C4fColor color, float coverage)
{
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    int a = (int)(coverage * color.a + 0.5f);
    if (a <= 0) return;
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    if (a >= 255) { *p = 0xff000000u | (uint32_t)color.r << 16 | (uint32_t)color.g << 8 | color.b; return; }
    uint32_t d = *p;
    unsigned inv = 255u - (unsigned)a;
    unsigned r = (((d >> 16) & 255u) * inv + color.r * (unsigned)a + 127u) / 255u;
    unsigned g = (((d >> 8) & 255u) * inv + color.g * (unsigned)a + 127u) / 255u;
    unsigned b = ((d & 255u) * inv + color.b * (unsigned)a + 127u) / 255u;
    *p = 0xff000000u | r << 16 | g << 8 | b;
}

void c4fFillRect(C4fCanvas *c, int x, int y, int w, int h, C4fColor color)
{
    for (int py = c4fMaxI(y, 0); py < c4fMinI(y + h, c->h); py++)
        for (int px = c4fMaxI(x, 0); px < c4fMinI(x + w, c->w); px++) c4fBlend(c, px, py, color, 1);
}

typedef struct { int x0, y0, x1, y1; } C4fBox;

static int c4fClip(const C4fCanvas *c, float x0, float y0, float x1, float y1, C4fBox *b)
{
    b->x0 = c4fMaxI((int)floorf(x0), 0);
    b->y0 = c4fMaxI((int)floorf(y0), 0);
    b->x1 = c4fMinI((int)ceilf(x1), c->w);
    b->y1 = c4fMinI((int)ceilf(y1), c->h);
    return b->x0 < b->x1 && b->y0 < b->y1;
}

/* Signed distance to a rounded box (centre, half size, corner radius). */
float c4fRoundBoxDistance(float px, float py, float cx, float cy, float hx, float hy, float r)
{
    float qx = fabsf(px - cx) - (hx - r), qy = fabsf(py - cy) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, inner = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (inner < 0 ? inner : 0) - r;
}

typedef struct { float cx, cy, hx, hy, r; } C4fShape;

static C4fShape c4fShape(float x, float y, float w, float h, float r)
{
    C4fShape s = { x + w / 2, y + h / 2, w / 2, h / 2, r };
    if (s.r > s.hx) s.r = s.hx;
    if (s.r > s.hy) s.r = s.hy;
    if (s.r < 0) s.r = 0;
    return s;
}

static float c4fShapeDistance(const C4fShape *s, int px, int py)
{
    return c4fRoundBoxDistance(px + 0.5f, py + 0.5f, s->cx, s->cy, s->hx, s->hy, s->r);
}

void c4fFillRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, C4fColor fill)
{
    C4fShape s = c4fShape(x, y, w, h, r);
    C4fBox b;
    if (!c4fClip(c, x - 1, y - 1, x + w + 1, y + h + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float cov = c4fClamp01(0.5f - c4fShapeDistance(&s, px, py));
            if (cov > 0) c4fBlend(c, px, py, fill, cov);
        }
}

void c4fInsetRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float width, C4fColor line)
{
    C4fShape s = c4fShape(x, y, w, h, r);
    C4fBox b;
    if (!c4fClip(c, x - 1, y - 1, x + w + 1, y + h + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float d = c4fShapeDistance(&s, px, py);
            float cov = c4fClamp01(0.5f - d) - c4fClamp01(0.5f - (d + width));
            if (cov > 0) c4fBlend(c, px, py, line, cov);
        }
}

void c4fSheenRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float angle,
                       C4fColor color, float a0, float a1, float stop)
{
    C4fShape s = c4fShape(x, y, w, h, r);
    C4fBox b;
    /* CSS gradient angles: 0deg points up, clockwise. */
    float rad = angle * 3.14159265f / 180.0f, dx = sinf(rad), dy = -cosf(rad);
    float length = fabsf(w * dx) + fabsf(h * dy);
    if (!c4fClip(c, x - 1, y - 1, x + w + 1, y + h + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float cov = c4fClamp01(0.5f - c4fShapeDistance(&s, px, py));
            if (cov <= 0) continue;
            float t = ((px + 0.5f - s.cx) * dx + (py + 0.5f - s.cy) * dy) / length + 0.5f;
            float a = t >= stop ? a1 : t <= 0 ? a0 : a0 + (a1 - a0) * (t / stop);
            c4fBlend(c, px, py, color, cov * a);
        }
}

void c4fBandRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r, float height, C4fColor color)
{
    C4fShape s = c4fShape(x, y, w, h, r);
    C4fBox b;
    if (!c4fClip(c, x - 1, y - 1, x + w + 1, y + height + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++) {
        float band = c4fClamp01(fminf(py + 1.0f, y + height) - fmaxf((float)py, y));
        if (band <= 0) continue;
        for (int px = b.x0; px < b.x1; px++) {
            float cov = c4fClamp01(0.5f - c4fShapeDistance(&s, px, py)) * band;
            if (cov > 0) c4fBlend(c, px, py, color, cov);
        }
    }
}

/* A blurred edge: the normal CDF, approximated by a logistic curve. */
static float c4fSoftEdge(float distance, float sigma)
{
    return 1.0f / (1.0f + expf(1.702f * distance / sigma));
}

void c4fShadowRoundRect(C4fCanvas *c, float x, float y, float w, float h, float r,
                        float dx, float dy, float blur, float spread, C4fColor color)
{
    float sigma = blur > 1 ? blur / 2 : 0.5f, reach = 3 * sigma;
    C4fShape own = c4fShape(x, y, w, h, r);
    C4fShape shadow = c4fShape(x + dx - spread, y + dy - spread, w + 2 * spread, h + 2 * spread, r + spread);
    C4fBox b;
    if (shadow.hx <= 0 || shadow.hy <= 0) return;
    if (!c4fClip(c, fminf(x, x + dx - spread) - reach, fminf(y, y + dy - spread) - reach,
                 fmaxf(x + w, x + dx + w + spread) + reach, fmaxf(y + h, y + dy + h + spread) + reach, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            /* An outer box-shadow is never painted under the element itself. */
            float inside = c4fClamp01(0.5f - c4fShapeDistance(&own, px, py));
            if (inside >= 1) continue;
            float d = c4fShapeDistance(&shadow, px, py);
            if (d > reach) continue;
            c4fBlend(c, px, py, color, c4fSoftEdge(d, sigma) * (1 - inside));
        }
}

void c4fFillCircle(C4fCanvas *c, float cx, float cy, float r, C4fColor fill)
{
    C4fBox b;
    if (!c4fClip(c, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float cov = c4fClamp01(0.5f - (hypotf(px + 0.5f - cx, py + 0.5f - cy) - r));
            if (cov > 0) c4fBlend(c, px, py, fill, cov);
        }
}

void c4fRing(C4fCanvas *c, float cx, float cy, float r, float width, C4fColor line)
{
    C4fBox b;
    if (!c4fClip(c, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float d = hypotf(px + 0.5f - cx, py + 0.5f - cy) - r;
            float cov = c4fClamp01(0.5f - d) - c4fClamp01(0.5f - (d + width));
            if (cov > 0) c4fBlend(c, px, py, line, cov);
        }
}

void c4fGlowCircle(C4fCanvas *c, float cx, float cy, float r, float blur, float spread, C4fColor color)
{
    float sigma = blur > 1 ? blur / 2 : 0.5f, reach = r + spread + 3 * sigma;
    C4fBox b;
    if (!c4fClip(c, cx - reach, cy - reach, cx + reach, cy + reach, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float distance = hypotf(px + 0.5f - cx, py + 0.5f - cy);
            float inside = c4fClamp01(0.5f - (distance - r));
            if (inside >= 1) continue;
            c4fBlend(c, px, py, color, c4fSoftEdge(distance - (r + spread), sigma) * (1 - inside));
        }
}

void c4fSegment(C4fCanvas *c, float x0, float y0, float x1, float y1, float width, C4fColor color)
{
    float hw = width / 2, vx = x1 - x0, vy = y1 - y0, length2 = vx * vx + vy * vy;
    C4fBox b;
    if (!c4fClip(c, fminf(x0, x1) - hw - 1, fminf(y0, y1) - hw - 1, fmaxf(x0, x1) + hw + 1, fmaxf(y0, y1) + hw + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float fx = px + 0.5f - x0, fy = py + 0.5f - y0;
            float t = length2 > 0 ? c4fClamp01((fx * vx + fy * vy) / length2) : 0;
            float cov = c4fClamp01(0.5f - (hypotf(fx - t * vx, fy - t * vy) - hw));
            if (cov > 0) c4fBlend(c, px, py, color, cov);
        }
}

void c4fSquareOutline(C4fCanvas *c, float cx, float cy, float half, float width, C4fColor color)
{
    float reach = half + width;
    C4fBox b;
    if (!c4fClip(c, cx - reach - 1, cy - reach - 1, cx + reach + 1, cy + reach + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            float d = c4fRoundBoxDistance(px + 0.5f, py + 0.5f, cx, cy, half, half, width * 0.4f);
            float cov = c4fClamp01(0.5f - (fabsf(d) - width / 2));
            if (cov > 0) c4fBlend(c, px, py, color, cov);
        }
}

void c4fTriangleOutline(C4fCanvas *c, float cx, float cy, float radius, float width, C4fColor color)
{
    /* An upward equilateral triangle through three corners at `radius`. */
    const float k = 1.7320508f;
    float reach = radius + width;
    C4fBox b;
    if (!c4fClip(c, cx - reach - 1, cy - reach - 1, cx + reach + 1, cy + reach + 1, &b)) return;
    for (int py = b.y0; py < b.y1; py++)
        for (int px = b.x0; px < b.x1; px++) {
            /* Signed distance to an equilateral triangle (Inigo Quilez). */
            float x = fabsf(px + 0.5f - cx), y = -(py + 0.5f - cy) * 1.0f, r = radius * 0.866025f;
            x = x - r;
            y = y + r / k;
            if (x + k * y > 0) { float nx = (x - k * y) / 2, ny = (-k * x - y) / 2; x = nx; y = ny; }
            x -= fminf(fmaxf(x, -2 * r), 0);
            float d = -hypotf(x, y) * (y < 0 ? -1 : 1);
            float cov = c4fClamp01(0.5f - (fabsf(d) - width / 2));
            if (cov > 0) c4fBlend(c, px, py, color, cov);
        }
}

/* ---- text ---- */

static C4fFace *c4fFace(int font, float size)
{
    int pixels = (int)(size + 0.5f);
    for (int i = 0; i < c4fFaceCount; i++)
        if (c4fFaces[i].font == font && c4fFaces[i].size == pixels) return &c4fFaces[i];
    if (font < 0 || font >= C4F_FONT_COUNT || c4fFaceCount == (int)(sizeof(c4fFaces) / sizeof(c4fFaces[0]))) return NULL;
    C4fFace *face = &c4fFaces[c4fFaceCount++];
    memset(face, 0, sizeof(*face));
    face->font = font;
    face->size = pixels;
    /* CSS font-size is the em size. */
    face->scale = stbtt_ScaleForMappingEmToPixels(&c4fFonts[font], (float)pixels);
    return face;
}

static int c4fCodepoint(unsigned char ch) { return ch < 32 || ch > 126 ? '?' : ch; }

static C4fGlyph *c4fGlyph(C4fFace *face, int ch)
{
    C4fGlyph *g = &face->glyph[ch - 32];
    if (!g->ready) {
        const stbtt_fontinfo *font = &c4fFonts[face->font];
        int advance, bearing, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(font, ch, &advance, &bearing);
        stbtt_GetCodepointBitmapBox(font, ch, face->scale, face->scale, &x0, &y0, &x1, &y1);
        g->advance = advance * face->scale;
        g->x0 = x0;
        g->y0 = y0;
        g->w = x1 - x0;
        g->h = y1 - y0;
        if (g->w > 0 && g->h > 0 && (g->bitmap = malloc((size_t)g->w * (size_t)g->h)))
            stbtt_MakeCodepointBitmap(font, g->bitmap, g->w, g->h, g->w, face->scale, face->scale, ch);
        g->ready = 1;
    }
    return g;
}

static float c4fTextWidthN(int font, float size, const char *text, int length)
{
    C4fFace *face = c4fFace(font, size);
    float width = 0;
    int previous = 0;
    if (!face) return 0;
    for (int i = 0; i < length && text[i]; i++) {
        int ch = c4fCodepoint((unsigned char)text[i]);
        if (previous) width += stbtt_GetCodepointKernAdvance(&c4fFonts[font], previous, ch) * face->scale;
        width += c4fGlyph(face, ch)->advance;
        previous = ch;
    }
    return width;
}

float c4fTextWidth(int font, float size, const char *text)
{
    return c4fTextWidthN(font, size, text, (int)strlen(text));
}

float c4fTextN(C4fCanvas *c, int font, float size, float x, float y, C4fColor color, const char *text, int length)
{
    C4fFace *face = c4fFace(font, size);
    float pen = x;
    int previous = 0, baseline = (int)floorf(y + 0.5f);
    if (!face) return 0;
    for (int i = 0; i < length && text[i]; i++) {
        int ch = c4fCodepoint((unsigned char)text[i]);
        if (previous) pen += stbtt_GetCodepointKernAdvance(&c4fFonts[font], previous, ch) * face->scale;
        C4fGlyph *g = c4fGlyph(face, ch);
        if (g->bitmap) {
            int gx = (int)floorf(pen + 0.5f) + g->x0, gy = baseline + g->y0;
            for (int row = 0; row < g->h; row++)
                for (int col = 0; col < g->w; col++) {
                    unsigned value = g->bitmap[row * g->w + col];
                    if (value) c4fBlend(c, gx + col, gy + row, color, c4fTextGamma[value] / 255.0f);
                }
        }
        pen += g->advance;
        previous = ch;
    }
    return pen - x;
}

float c4fText(C4fCanvas *c, int font, float size, float x, float y, C4fColor color, const char *text)
{
    return c4fTextN(c, font, size, x, y, color, text, (int)strlen(text));
}

int c4fTextWrapped(C4fCanvas *c, int font, float size, float x, float y, float maxWidth,
                   float lineHeight, int maxLines, C4fColor color, const char *text)
{
    int lines = 0;
    while (*text && lines < maxLines) {
        while (*text == ' ') text++;
        if (!*text) break;
        int fit = 0, end = 0;
        for (;;) {
            while (text[end] && text[end] != ' ') end++;
            if (fit && c4fTextWidthN(font, size, text, end) > maxWidth) break;
            fit = end;
            if (!text[end]) break;
            end++;
        }
        if (c) c4fTextN(c, font, size, x, y + lines * lineHeight, color, text, fit);
        lines++;
        text += fit;
    }
    return lines;
}
