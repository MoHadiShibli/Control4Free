/* Reuse the real tester fixtures; compare fresh and language-switched frames. */
#define main c4fOriginalRendererMain
#include "tester_render_test.c"
#undef main
#include "language.h"
#include "draw.h"
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
    } else {
        assert(argc == 4);
        const int language = !strcmp(argv[1], "pt") ? C4F_LANGUAGE_PORTUGUESE : C4F_LANGUAGE_ENGLISH;
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
