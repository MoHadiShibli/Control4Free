/* The app's Portuguese: the whole table against what the fonts can draw, the
 * texts the app fills in, and when the translation is used. The SDK's system
 * language call and draw.c's hook are replaced. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../launcher/language.c"

static const char *(*shownWith)(const char *text);
static unsigned changes;
static int systemLanguage = 1, systemError;

void c4fSetTextTranslation(const char *(*translate)(const char *text))
{
    shownWith = translate;
    changes++;
}

unsigned c4fTextTranslationGeneration(void) { return changes; }

int sceSystemServiceParamGetInt(int id, int *value)
{
    assert(id == C4F_SYSTEM_PARAM_LANG);
    *value = systemLanguage;
    return systemError;
}

/* draw.c shows two-byte UTF-8 as Latin-1 and anything longer as '?'. */
static int drawable(const char *text)
{
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        if (*s < 0x80) { if (*s < 32 || *s == 127) return 0; continue; }
        if (s[0] != 0xc2 && s[0] != 0xc3) return 0;
        if ((s[1] & 0xc0) != 0x80 || (s[0] == 0xc2 && s[1] < 0xa0)) return 0;
        s++;
    }
    return 1;
}

/* The conversions in order, as one string ("%d%s" for "%d of %s"). */
static void conversions(const char *text, char *out, size_t size)
{
    size_t used = 0;
    for (const char *at = text; *at; at++) {
        if (*at != '%') continue;
        const char *end = c4fConversionEnd(at);
        size_t length = (size_t)(end - at);
        assert(used + length < size);
        memcpy(out + used, at, length);
        used += length;
        at = end - 1;
    }
    out[used] = 0;
}

static void table(void)
{
    const size_t count = sizeof(c4fPortuguese) / sizeof(c4fPortuguese[0]);
    for (size_t i = 0; i < count; i++) {
        const C4fPhrase *p = &c4fPortuguese[i];
        char en[32], pt[32];
        assert(drawable(p->pt));
        conversions(p->en, en, sizeof(en));
        conversions(p->pt, pt, sizeof(pt));
        if (strcmp(en, pt)) { fprintf(stderr, "conversions differ: %s\n", p->en); assert(0); }
        for (size_t j = 0; j < i; j++) assert(strcmp(p->en, c4fPortuguese[j].en));
        assert(!strchr(p->en, '%') ? c4fPortugueseText(p->en) == p->pt : 1);
    }
    printf("PASS %zu Portuguese texts: drawable, unique, same filled-in values\n", count);
}

static void filledIn(void)
{
    assert(!strcmp(c4fPortugueseText("Close"), "Fechar"));
    assert(!strcmp(c4fPortugueseText("3 controllers connected"), "3 controles conectados"));
    assert(!strcmp(c4fPortugueseText("1 controller connected"), "1 controle conectado"));
    assert(!strcmp(c4fPortugueseText("Cannot read (0x80020001)"), "Não foi possível ler (0x80020001)"));
    assert(!strcmp(c4fPortugueseText("Installed 1.3.0; running 1.2.1"), "Instalado 1.3.0; rodando 1.2.1"));
    /* A filled-in text the app also translates, and one it doesn't. */
    assert(!strcmp(c4fPortugueseText("Auto-start is on. Ready. Open the address on your phone or PC."),
                   "Início automático ligado. Pronto. Abra o endereço no seu celular ou PC."));
    assert(!strcmp(c4fPortugueseText("Sent, but Control4Free did not start (127.0.0.1:4264 no reply). Retry Start."),
                   "Enviado, mas o Control4Free não iniciou (127.0.0.1:4264 no reply). Tente Iniciar de novo."));
    assert(!strcmp(c4fPortugueseText("Sent, but Control4Free did not start (the status reply is not from Control4Free). Retry Start."),
                   "Enviado, mas o Control4Free não iniciou (a resposta de status não é do Control4Free). Tente Iniciar de novo."));
    /* Several results stay valid at once: a text, then its width. */
    const char *first = c4fPortugueseText("User 1"), *second = c4fPortugueseText("User 2");
    assert(!strcmp(first, "Usuário 1") && !strcmp(second, "Usuário 2"));
    /* Names, numbers and near misses are shown as they are. */
    assert(!c4fPortugueseText("Lucas") && !c4fPortugueseText("") && !c4fPortugueseText(NULL));
    assert(!c4fPortugueseText("Control4Free 1.3.0") && !c4fPortugueseText(" controllers connected"));
    assert(!c4fPortugueseText("Close "));
    puts("PASS filled-in values, nested translations and untranslated texts");
}

static void choice(void)
{
    systemLanguage = 1; /* English (United States) */
    c4fLanguageUse(C4F_LANGUAGE_AUTO);
    assert(!shownWith);
    unsigned before = changes;
    c4fLanguageUse(C4F_LANGUAGE_ENGLISH);
    assert(!shownWith && changes == before);
    c4fLanguageUse(C4F_LANGUAGE_PORTUGUESE);
    assert(shownWith == c4fPortugueseText);
    c4fLanguageUse(C4F_LANGUAGE_ENGLISH);
    assert(!shownWith);
    for (int language = 7; language <= 17; language += 10) { /* Portuguese, then Portuguese (Brazil) */
        systemLanguage = language;
        c4fLanguageUse(C4F_LANGUAGE_AUTO);
        assert(shownWith == c4fPortugueseText);
        c4fLanguageUse(C4F_LANGUAGE_ENGLISH);
    }
    systemLanguage = 17; systemError = -1;
    c4fLanguageUse(C4F_LANGUAGE_AUTO);
    assert(!shownWith);
    puts("PASS automatic follows the PS4's Portuguese; English and Portuguese override it");
}

int main(void)
{
    table();
    filledIn();
    choice();
    return 0;
}
