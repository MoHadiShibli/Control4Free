/* Exercise the shipped shaping and bidi libraries with the shipped static fonts. */
#include <hb.h>
#include <hb-ot.h>
#include <SheenBidi/SheenBidi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "text dependency check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); \
    exit(1); } } while (0)

static void c4fTestFont(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0);
    rewind(file);
    char *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    hb_blob_t *blob = hb_blob_create(bytes, (unsigned)size, HB_MEMORY_MODE_READONLY, bytes, free);
    hb_face_t *face = hb_face_create(blob, 0);
    hb_font_t *font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, 32 * 64, 32 * 64);

    hb_codepoint_t nominal = 0;
    CHECK(hb_font_get_nominal_glyph(font, 0x0628, &nominal)); /* beh */
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, "ببب", -1, 0, -1);
    hb_buffer_set_direction(buffer, HB_DIRECTION_RTL);
    hb_buffer_set_script(buffer, HB_SCRIPT_ARABIC);
    hb_buffer_set_language(buffer, hb_language_from_string("ar", -1));
    hb_shape(font, buffer, NULL, 0);
    unsigned count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    /* Noto Arabic decomposes each beh into a dot and a contextual body. */
    CHECK(count == 6);
    CHECK(infos[1].cluster == 4 && infos[3].cluster == 2 && infos[5].cluster == 0);
    CHECK(infos[1].codepoint && infos[3].codepoint && infos[5].codepoint);
    CHECK(infos[1].codepoint != nominal && infos[3].codepoint != nominal &&
          infos[5].codepoint != nominal);
    CHECK(infos[1].codepoint != infos[3].codepoint && infos[3].codepoint != infos[5].codepoint);

    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, "مَرْحَبًا", -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);
    infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, NULL);
    unsigned marks = 0;
    for (unsigned i = 0; i < count; ++i) {
        CHECK(infos[i].codepoint != 0);
        if (!positions[i].x_advance && (positions[i].x_offset || positions[i].y_offset))
            ++marks;
    }
    CHECK(marks >= 3); /* combining marks attach instead of consuming spacing */
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

static void c4fTestBidi(void)
{
    const char *text = "مرحبا \xE2\x81\xA8" "PS4 192.168.1.20" "\xE2\x81\xA9" " (123)";
    SBCodepointSequence sequence = {SBStringEncodingUTF8, text, strlen(text)};
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    CHECK(algorithm);
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, sequence.stringLength,
                                                         SBLevelDefaultRTL);
    CHECK(paragraph && SBParagraphGetBaseLevel(paragraph) == 1);
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, sequence.stringLength);
    CHECK(line);
    const SBRun *runs = SBLineGetRunsPtr(line);
    SBUInteger count = SBLineGetRunCount(line);
    CHECK(count >= 3);
    unsigned latin = 0;
    for (SBUInteger i = 0; i < count; ++i) {
        if (!(runs[i].level & 1) && runs[i].length >= strlen("PS4 192.168.1.20") &&
            memcmp(text + runs[i].offset, "PS4 192.168.1.20", strlen("PS4 192.168.1.20")) == 0)
            ++latin;
    }
    CHECK(latin == 1); /* isolated technical values remain LTR */
    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    CHECK(hb_version_atleast(11, 2, 1));
    c4fTestFont(argv[1]);
    c4fTestFont(argv[2]);
    c4fTestBidi();
    puts("Arabic text dependencies: static fonts, joining, marks, bidi PASS");
    return 0;
}
