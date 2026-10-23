/* Reuse the real tester fixtures; compare fresh and language-switched frames. */
#include "draw.h"
#define main c4fOriginalRendererMain
#include "tester_render_test.c"
#undef main
#include "language.h"
#include <assert.h>

int sceSystemServiceParamGetInt(int id, int *value)
{
    (void)id;
    *value = 1;
    return 0;
}

static int c4fRawNameSeen;
static const char *c4fNameGuard(const char *text)
{
    if (!strcmp(text, "RawProfileNameMarker")) c4fRawNameSeen++;
    return c4fPortugueseText(text);
}

static void c4fMetadataFixture(C4fLauncherScreen *screen, int which)
{
    c4fFixture(screen, c4fCaseId("menu"));
    if (which < 2) {
        /* A real username may equal the English fallback. Only the generated
         * flag distinguishes whether that text is translated. */
        snprintf(screen->tester.user[0].name, sizeof(screen->tester.user[0].name), "User 1");
        screen->tester.user[0].nameGenerated = which;
    } else {
        /* The English diagnostic is identical, while explicit argument
         * provenance controls whether this value is an interface key. */
        c4fMessageWrite(&screen->tester.noteText, screen->tester.note,
                        sizeof(screen->tester.note), "The app cannot reach Control4Free: %s", "Left");
        screen->tester.noteText.valueIsKey = which % 2;
    }
}

int main(int argc, char **argv)
{
    assert(!c4fScreenInit());
    uint32_t *pixels = calloc((size_t)C4F_SCREEN_WIDTH * C4F_SCREEN_HEIGHT, sizeof(*pixels));
    assert(pixels);
    C4fLauncherScreen screen;
    if (argc == 2 && !strcmp(argv[1], "names")) {
        c4fSetTextTranslation(c4fNameGuard);
        for (int i = 0; i < 3; i++) {
            c4fFixture(&screen, c4fCaseId(i == 0 ? "idle" : i == 1 ? "menu" : "color-red"));
            snprintf(screen.tester.user[0].name, sizeof(screen.tester.user[0].name), "RawProfileNameMarker");
            c4fDrawTester(pixels, &screen);
        }
        assert(!c4fRawNameSeen);
        assert(c4fTextWidthRaw(C4F_FONT_REGULAR, 26, "Left") != c4fTextWidth(C4F_FONT_REGULAR, 26, "Left"));
        puts("PASS names bypass interface translation in tabs, selected user and touchpad");
    } else if (argc == 5 && !strcmp(argv[1], "metadata")) {
        c4fLanguageUse(!strcmp(argv[2], "ar") ? C4F_LANGUAGE_ARABIC : C4F_LANGUAGE_PORTUGUESE);
        int which = atoi(argv[4]);
        assert(which >= 0 && which < 4);
        if (!strcmp(argv[3], "warm")) {
            c4fMetadataFixture(&screen, which ^ 1);
            c4fDrawTester(pixels, &screen);
        }
        c4fMetadataFixture(&screen, which);
        c4fDrawTester(pixels, &screen);
        printf("%016llx\n", (unsigned long long)c4fHash(pixels));
    } else {
        assert(argc == 4);
        const int language = !strcmp(argv[1], "pt") ? C4F_LANGUAGE_PORTUGUESE :
                             !strcmp(argv[1], "ar") ? C4F_LANGUAGE_ARABIC : C4F_LANGUAGE_ENGLISH;
        if (!strcmp(argv[2], "warm")) {
            c4fLanguageUse(language == C4F_LANGUAGE_ENGLISH ? C4F_LANGUAGE_PORTUGUESE : C4F_LANGUAGE_ENGLISH);
            for (int i = 0; i < 6; i++) {
                c4fFixture(&screen, c4fCaseId("color-red"));
                screen.tester.colorChannel = i % 3;
                screen.tester.colorFine = i / 3;
                c4fDrawTester(pixels, &screen);
            }
        }
        c4fLanguageUse(language);
        c4fFixture(&screen, c4fCaseId("color-red"));
        int which = atoi(argv[3]);
        screen.tester.colorChannel = which % 3;
        screen.tester.colorFine = which / 3;
        c4fDrawTester(pixels, &screen);
        printf("%016llx\n", (unsigned long long)c4fHash(pixels));
    }
    free(pixels);
    return 0;
}
