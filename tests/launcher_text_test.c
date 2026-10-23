/* Exercise the shipped Unicode layout/cache internals as well as public
 * measurement and drawing; the console font may be supplied by the harness. */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../launcher/text.c"

static uint32_t pixels[800 * 300];
static C4fCanvas canvas = { pixels, 800, 300 };
static const C4fColor white = { 255, 255, 255, 255 };

static uint64_t hash(void)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) { h ^= pixels[i]; h *= 1099511628211ULL; }
    return h;
}

static void draw(const char *text)
{
    memset(pixels, 0, sizeof(pixels));
    float width = c4fTextWidthRaw(C4F_FONT_REGULAR, 36, text);
    float drawn = c4fTextRaw(&canvas, C4F_FONT_REGULAR, 36, 100, 120, white, text);
    assert(width > 0 && fabsf(width - drawn) < .01f);
}

int main(int argc, char **argv)
{
    assert(c4fDrawInit() == 0);
    assert(c4fTextSetArabicFonts((const unsigned char *)"invalid", 7, (const unsigned char *)"invalid", 7) == -1);
    if (argc == 3) {
        size_t lightSize = 0, regularSize = 0;
        unsigned char *light = c4fReadFont(argv[1], &lightSize), *regular = c4fReadFont(argv[2], &regularSize);
        assert(light && regular);
        draw("مرحبا");
        unsigned generation = c4fTextTranslationGeneration();
        assert(c4fTextSetArabicFonts(light, lightSize, regular, regularSize) == 0);
        assert(c4fTextTranslationGeneration() != generation && c4fRasterBytes == 0);
        /* The bytes stay alive until process exit, just as in the launcher. */
    } else if (argc == 2 && !strcmp(argv[1], "--system")) assert(!c4fTextLoadSystemFonts());
    else { assert(argc == 1); assert(c4fTextLoadSystemFonts() == -1); }
    const char *technical = "Installed dev; 192.168.1.20:4264";
    assert(!c4fTextNeedsShaping(technical, (int)strlen(technical)));
    assert(c4fTextNeedsShaping("مرحبا", (int)strlen("مرحبا")));
    C4fTextLayout *joined = c4fShape(C4F_FONT_REGULAR, 36, "ببب", (int)strlen("ببب"));
    assert(joined && joined->count >= 3 && joined->width > 0);
    unsigned nominal = (unsigned)stbtt_FindGlyphIndex(&c4fArabicSTB[C4F_FONT_REGULAR], 0x0628);
    int contextual = 0;
    for (int i = 0; i < joined->count; i++) {
        assert(joined->glyphs[i].id);
        contextual |= joined->glyphs[i].id != nominal;
    }
    assert(contextual); /* Arabic joining actually selects contextual glyph IDs. */
    C4fTextLayout *marks = c4fShape(C4F_FONT_REGULAR, 36, "مَرْحَبًا", (int)strlen("مَرْحَبًا"));
    assert(marks && marks->count > 5);
    int positioned = 0;
    for (int i = 0; i < marks->count; i++) positioned |= marks->glyphs[i].y != 0;
    assert(positioned); /* Diacritics use shaping offsets rather than spacing. */

    const char *mixed = "مرحبا \xE2\x81\xA8" "192.168.1.20" "\xE2\x81\xA9";
    C4fTextLayout *layout = c4fShape(C4F_FONT_REGULAR, 36, mixed, (int)strlen(mixed));
    assert(layout);
    unsigned one = (unsigned)stbtt_FindGlyphIndex(&c4fArabicSTB[C4F_FONT_REGULAR], '1');
    unsigned nine = (unsigned)stbtt_FindGlyphIndex(&c4fArabicSTB[C4F_FONT_REGULAR], '9');
    unsigned two = (unsigned)stbtt_FindGlyphIndex(&c4fArabicSTB[C4F_FONT_REGULAR], '2');
    int digits = 0;
    for (int i = 0; i + 2 < layout->count; i++)
        digits |= layout->glyphs[i].id == one && layout->glyphs[i + 1].id == nine && layout->glyphs[i + 2].id == two;
    assert(digits); /* The IP's digit sequence stays LTR inside Arabic. */
    draw(mixed); uint64_t original = hash();
    for (int i = 0; i < 600; i++) {
        char sample[128]; snprintf(sample, sizeof(sample), "المستخدم Alex (%d)", i);
        c4fTextRaw(&canvas, i & 1, (float)(12 + i % 110), 50, 150, white, sample);
        assert(c4fRasterBytes <= C4F_RASTER_BYTES);
    }
    draw(mixed); assert(hash() == original); /* Cache eviction cannot change layout/raster results. */
    c4fSetTextDirection(1);
    memset(pixels, 0, sizeof(pixels));
    assert(c4fTextWrapped(&canvas, C4F_FONT_REGULAR, 36, 100, 120, 500, 60, 1, white, "مرحبا") == 1);
    int left = 800, right = -1;
    for (int y = 0; y < canvas.h; y++) for (int x = 0; x < canvas.w; x++) if (pixels[y * canvas.w + x]) {
        if (x < left) left = x;
        if (x > right) right = x;
    }
    assert(left > 350 && right <= 610);
    c4fSetTextDirection(0);
    draw("مَرْحَبًا Alex (PS4) 123");
    puts("PASS native Unicode: Arabic joining, marks, mixed-direction numbers, matching width/draw, RTL wrap, bounded cache eviction");
    return 0;
}
