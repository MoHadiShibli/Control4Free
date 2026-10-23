/* Unicode layout: the Unicode bidi algorithm supplies visual runs; HarfBuzz
 * shapes each run; the existing stb rasterizer draws the resulting glyph IDs.
 * All caches are bounded and belong exclusively to the drawing thread. */
#include "text.h"
#include "stb_truetype.h"
#include <hb.h>
#include <hb-ot.h>
#include <SheenBidi/SheenBidi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern const unsigned char c4fFontArabicLight[], c4fFontArabicRegular[];
extern const size_t c4fFontArabicLightSize, c4fFontArabicRegularSize;
enum { C4F_LAYOUTS = 32, C4F_LAYOUT_BYTES = 2048, C4F_LAYOUT_GLYPHS = 1024,
       C4F_RASTERS = 512, C4F_RASTER_BYTES = 4 * 1024 * 1024 };
typedef struct { unsigned id; float x, y; } C4fPositionedGlyph;
typedef struct {
    char text[C4F_LAYOUT_BYTES];
    int length, font, pixels, rtl, count, ready;
    unsigned used;
    float width;
    C4fPositionedGlyph glyphs[C4F_LAYOUT_GLYPHS];
} C4fTextLayout;
typedef struct {
    int font, pixels, w, h, x, y, ready;
    unsigned glyph, used;
    unsigned char *bitmap;
} C4fGlyphRaster;
static hb_font_t *c4fArabicHB[C4F_FONT_COUNT];
static stbtt_fontinfo c4fArabicSTB[C4F_FONT_COUNT];
static C4fTextLayout c4fLayouts[C4F_LAYOUTS];
static C4fGlyphRaster c4fRasters[C4F_RASTERS];
static size_t c4fRasterBytes;
static unsigned c4fTextTick;
static int c4fRTL;
static uint8_t c4fGlyphGamma[256];
static const unsigned char *c4fArabicData[C4F_FONT_COUNT];
static size_t c4fArabicSizes[C4F_FONT_COUNT];
static int c4fSystemFontsLoaded;

void c4fSetTextDirection(int rtl) { c4fRTL = !!rtl; }
int c4fTextIsRTL(void) { return c4fRTL; }

static unsigned c4fBE16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t c4fBE32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int c4fValidFont(const unsigned char *data, size_t size)
{
    if (!data || size < 12 || size > 1024 * 1024 ||
        (memcmp(data, "OTTO", 4) && c4fBE32(data) != 0x00010000)) return 0;
    unsigned tables = c4fBE16(data + 4);
    if (!tables || (size_t)tables > (size - 12) / 16) return 0;
    for (unsigned i = 0; i < tables; i++) {
        const unsigned char *entry = data + 12 + 16 * i;
        uint32_t offset = c4fBE32(entry + 8), length = c4fBE32(entry + 12);
        if (offset > size || length > size - offset) return 0;
    }
    stbtt_fontinfo info;
    return stbtt_InitFont(&info, data, 0) && stbtt_FindGlyphIndex(&info, 0x0628) != 0;
}

int c4fTextSetArabicFonts(const unsigned char *light, size_t lightSize,
                         const unsigned char *regular, size_t regularSize)
{
    if (!c4fValidFont(light, lightSize) || !c4fValidFont(regular, regularSize)) return -1;
    for (int i = 0; i < C4F_FONT_COUNT; i++) {
        if (c4fArabicHB[i]) hb_font_destroy(c4fArabicHB[i]);
        c4fArabicHB[i] = NULL;
    }
    for (int i = 0; i < C4F_RASTERS; i++) free(c4fRasters[i].bitmap);
    memset(c4fRasters, 0, sizeof(c4fRasters)); c4fRasterBytes = 0;
    memset(c4fLayouts, 0, sizeof(c4fLayouts));
    c4fArabicData[0] = light; c4fArabicData[1] = regular;
    c4fArabicSizes[0] = lightSize; c4fArabicSizes[1] = regularSize;
    int result = c4fShapedTextInit();
    c4fTextFontChanged();
    return result;
}

static unsigned char *c4fReadFont(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    unsigned char *bytes = NULL;
    if (!fseek(file, 0, SEEK_END)) {
        long length = ftell(file);
        if (length >= 12 && length <= 1024 * 1024 && !fseek(file, 0, SEEK_SET)) {
            bytes = malloc((size_t)length);
            if (bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length && c4fValidFont(bytes, (size_t)length)) *size = (size_t)length;
            else { free(bytes); bytes = NULL; }
        }
    }
    fclose(file);
    return bytes;
}

int c4fTextLoadSystemFonts(void)
{
    if (c4fSystemFontsLoaded) return 0;
    static const char *const lightPaths[] = { "/preinst/common/font/SSTArabic-Light.otf", "/system/common/font2/SSTArabic-Light.otf" };
    static const char *const regularPaths[] = { "/preinst/common/font/SSTArabic-Roman.otf", "/system/common/font2/SSTArabic-Roman.otf" };
    unsigned char *bytes[C4F_FONT_COUNT] = { NULL, NULL };
    size_t sizes[C4F_FONT_COUNT] = { 0, 0 };
    for (unsigned i = 0; i < 2 && !bytes[0]; i++) bytes[0] = c4fReadFont(lightPaths[i], &sizes[0]);
    for (unsigned i = 0; i < 2 && !bytes[1]; i++) bytes[1] = c4fReadFont(regularPaths[i], &sizes[1]);
    if (bytes[0] && bytes[1] && !c4fTextSetArabicFonts(bytes[0], sizes[0], bytes[1], sizes[1])) {
        /* Retain these bounded, read-only files for the process lifetime. */
        c4fSystemFontsLoaded = 1;
        return 0;
    }
    free(bytes[0]); free(bytes[1]);
    return -1;
}

int c4fShapedTextInit(void)
{
    const unsigned char *data[] = { c4fArabicData[0] ? c4fArabicData[0] : c4fFontArabicLight,
                                  c4fArabicData[1] ? c4fArabicData[1] : c4fFontArabicRegular };
    const size_t sizes[] = { c4fArabicData[0] ? c4fArabicSizes[0] : c4fFontArabicLightSize,
                            c4fArabicData[1] ? c4fArabicSizes[1] : c4fFontArabicRegularSize };
    for (int i = 0; i < C4F_FONT_COUNT; i++) {
        if (c4fArabicHB[i]) continue;
        if (!stbtt_InitFont(&c4fArabicSTB[i], data[i], 0)) return -1;
        hb_blob_t *blob = hb_blob_create((const char *)data[i], (unsigned)sizes[i], HB_MEMORY_MODE_READONLY, NULL, NULL);
        hb_face_t *face = hb_face_create(blob, 0);
        c4fArabicHB[i] = hb_font_create(face);
        hb_ot_font_set_funcs(c4fArabicHB[i]);
        unsigned em = hb_face_get_upem(face);
        hb_font_set_scale(c4fArabicHB[i], (int)em, (int)em);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
    for (int i = 0; i < 256; i++) c4fGlyphGamma[i] = (uint8_t)(powf(i / 255.0f, .8f) * 255.0f + .5f);
    return 0;
}

int c4fTextNeedsShaping(const char *text, int length)
{
    /* ASCII and Latin-1 keep the original fast Roboto path. All other Unicode,
     * including direction isolates in translated templates, uses this path. */
    for (int i = 0; i < length && text[i]; i++)
        if ((unsigned char)text[i] >= 0xc4) return 1;
    return 0;
}

static C4fTextLayout *c4fShape(int font, float size, const char *text, int length)
{
    if (!text || length < 0 || font < 0 || font >= C4F_FONT_COUNT || !c4fArabicHB[font]) return NULL;
    int pixels = (int)(size + .5f);
    if (pixels <= 0 || pixels > 256) return NULL;
    if (length >= C4F_LAYOUT_BYTES) {
        length = C4F_LAYOUT_BYTES - 1;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) length--;
    }
    C4fTextLayout *out = &c4fLayouts[0];
    for (int i = 0; i < C4F_LAYOUTS; i++) {
        C4fTextLayout *p = &c4fLayouts[i];
        if (p->ready && p->font == font && p->pixels == pixels && p->rtl == c4fRTL &&
            p->length == length && !memcmp(p->text, text, (size_t)length)) {
            p->used = ++c4fTextTick;
            return p;
        }
        if (!p->ready || (out->ready && p->used < out->used)) out = p;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->text, text, (size_t)length);
    out->length = length; out->font = font; out->pixels = pixels; out->rtl = c4fRTL;
    out->used = ++c4fTextTick;
    SBCodepointSequence sequence = { SBStringEncodingUTF8, out->text, (SBUInteger)length };
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    if (!algorithm) return NULL;
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, (SBUInteger)length,
        c4fRTL ? SBLevelDefaultRTL : SBLevelDefaultLTR);
    SBLineRef line = paragraph ? SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph)) : NULL;
    if (line) {
        const SBRun *runs = SBLineGetRunsPtr(line);
        const float scale = stbtt_ScaleForMappingEmToPixels(&c4fArabicSTB[font], (float)pixels);
        for (SBUInteger i = 0; i < SBLineGetRunCount(line); i++) {
            hb_buffer_t *buffer = hb_buffer_create();
            hb_buffer_add_utf8(buffer, out->text, length, (unsigned)runs[i].offset, (int)runs[i].length);
            hb_buffer_guess_segment_properties(buffer);
            hb_buffer_set_direction(buffer, (runs[i].level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_shape(c4fArabicHB[font], buffer, NULL, 0);
            unsigned count;
            const hb_glyph_info_t *glyphs = hb_buffer_get_glyph_infos(buffer, &count);
            const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, NULL);
            for (unsigned j = 0; j < count && out->count < C4F_LAYOUT_GLYPHS; j++) {
                C4fPositionedGlyph *g = &out->glyphs[out->count++];
                g->id = glyphs[j].codepoint;
                g->x = out->width + positions[j].x_offset * scale;
                g->y = -positions[j].y_offset * scale;
                out->width += positions[j].x_advance * scale;
            }
            hb_buffer_destroy(buffer);
        }
        SBLineRelease(line);
    }
    if (paragraph) SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
    out->ready = line != NULL;
    return out->ready ? out : NULL;
}

static void c4fFreeRaster(C4fGlyphRaster *r)
{
    if (r->bitmap) c4fRasterBytes -= (size_t)r->w * (size_t)r->h;
    free(r->bitmap);
    memset(r, 0, sizeof(*r));
}

static C4fGlyphRaster *c4fRaster(int font, int pixels, unsigned glyph)
{
    C4fGlyphRaster *out = &c4fRasters[0];
    for (int i = 0; i < C4F_RASTERS; i++) {
        C4fGlyphRaster *r = &c4fRasters[i];
        if (r->ready && r->font == font && r->pixels == pixels && r->glyph == glyph) {
            r->used = ++c4fTextTick;
            return r;
        }
        if (!r->ready || (out->ready && r->used < out->used)) out = r;
    }
    c4fFreeRaster(out);
    const stbtt_fontinfo *face = &c4fArabicSTB[font];
    float scale = stbtt_ScaleForMappingEmToPixels(face, (float)pixels);
    int x1, y1;
    stbtt_GetGlyphBitmapBox(face, (int)glyph, scale, scale, &out->x, &out->y, &x1, &y1);
    out->w = x1 - out->x; out->h = y1 - out->y;
    size_t bytes = (size_t)out->w * (size_t)out->h;
    if (bytes > C4F_RASTER_BYTES) return NULL;
    while (c4fRasterBytes + bytes > C4F_RASTER_BYTES) {
        C4fGlyphRaster *oldest = NULL;
        for (int i = 0; i < C4F_RASTERS; i++)
            if (&c4fRasters[i] != out && c4fRasters[i].bitmap && (!oldest || c4fRasters[i].used < oldest->used)) oldest = &c4fRasters[i];
        if (!oldest) return NULL;
        c4fFreeRaster(oldest);
    }
    if (bytes) {
        out->bitmap = malloc(bytes);
        if (!out->bitmap) return NULL;
        stbtt_MakeGlyphBitmap(face, out->bitmap, out->w, out->h, out->w, scale, scale, (int)glyph);
        c4fRasterBytes += bytes;
    }
    out->font = font; out->pixels = pixels; out->glyph = glyph;
    out->used = ++c4fTextTick; out->ready = 1;
    return out;
}

float c4fShapedText(C4fCanvas *c, int font, float size, float x, float y,
                    C4fColor color, const char *text, int length)
{
    C4fTextLayout *layout = c4fShape(font, size, text, length);
    if (!layout) return 0;
    if (c) for (int i = 0; i < layout->count; i++) {
        const C4fPositionedGlyph *g = &layout->glyphs[i];
        C4fGlyphRaster *r = c4fRaster(font, layout->pixels, g->id);
        if (!r || !r->bitmap) continue;
        int left = (int)floorf(x + g->x + .5f) + r->x, top = (int)floorf(y + g->y + .5f) + r->y;
        for (int row = 0; row < r->h; row++) for (int col = 0; col < r->w; col++) {
            unsigned value = r->bitmap[row * r->w + col];
            if (value) c4fBlend(c, left + col, top + row, color, c4fGlyphGamma[value] / 255.0f);
        }
    }
    return layout->width;
}
