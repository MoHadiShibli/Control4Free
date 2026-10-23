/* Exact offline interface catalogs, formatting, and automatic language choice.
 * The SDK's system language call and renderer hooks are replaced. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../launcher/language.c"

static const char *(*shownWith)(const char *text);
static unsigned changes;
static int systemLanguage = 1, systemError;
static int rtl;

void c4fSetTextDirection(int value) { rtl = value; }

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
        const char *end = at + 1;
        while (*end && strchr("0123456789.-+ #lhz", *end)) end++;
        if (*end) end++;
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
        assert(c4fPortugueseText(p->en) == p->pt);
    }
    printf("PASS %zu Portuguese texts: drawable, unique, same filled-in values\n", count);
}

static void formattedKeys(void)
{
    assert(!strcmp(c4fPortugueseText("Close"), "Fechar"));
    assert(!strcmp(c4fPortugueseText("1 controller connected"), "1 controle conectado"));
    char out[512];
    snprintf(out, sizeof(out), c4fPortugueseText("%d controllers connected"), 3);
    assert(!strcmp(out, "3 controles conectados"));
    snprintf(out, sizeof(out), c4fPortugueseText("Cannot read (0x%08x)"), 0x80020001u);
    assert(!strcmp(out, "Não foi possível ler (0x80020001)"));
    /* Translate the template before inserting a literal value. A value that
     * happens to be an interface key must stay literal. */
    snprintf(out, sizeof(out), c4fPortugueseText("Sent, but Control4Free did not start (%s). Retry Start."), "Left");
    assert(!strcmp(out, "Enviado, mas o Control4Free não iniciou (Left). Tente Iniciar de novo."));
    assert(!c4fPortugueseText("3 controllers connected"));
    assert(!c4fPortugueseText("User 1"));
    assert(!c4fPortugueseText("Cannot read (0x80020001)"));
    /* Unknown keys and near misses remain untouched. */
    assert(!c4fPortugueseText("Lucas") && !c4fPortugueseText("") && !c4fPortugueseText(NULL));
    assert(!c4fPortugueseText(" controllers connected"));
    assert(!c4fPortugueseText("Close "));
    puts("PASS exact keys, formatting before value insertion, no filled-string matching");
}

static void arabicTable(void)
{
    const size_t count = sizeof(c4fArabic) / sizeof(c4fArabic[0]);
    for (size_t i = 0; i < count; ++i) {
        const C4fPhrase *p = &c4fArabic[i];
        char en[32], ar[32];
        conversions(p->en, en, sizeof(en));
        conversions(p->pt, ar, sizeof(ar));
        assert(!strcmp(en, ar));
        assert(c4fArabicText(p->en) == p->pt);
        for (size_t j = 0; j < i; ++j) assert(strcmp(p->en, c4fArabic[j].en));
        for (const unsigned char *s = (const unsigned char *)p->pt; *s; ++s)
            assert(!(s[0] == 0xd9 && s[1] >= 0xa0 && s[1] <= 0xa9)); /* keep digits 0-9 */
    }
    assert(count >= sizeof(c4fPortuguese) / sizeof(c4fPortuguese[0]));
    assert(!c4fArabicText("User 1") && !c4fArabicText(NULL));
    puts("PASS Arabic catalog: unique keys, matching format parameters, digits 0-9");
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
    assert(shownWith == c4fPortugueseText && !rtl);
    assert(!strcmp(c4fTr("Left"), "Esquerdo"));
    assert(!strcmp(c4fTr("Unknown service error"), "Unknown service error"));
    c4fLanguageUse(C4F_LANGUAGE_ARABIC);
    assert(shownWith == c4fArabicText && rtl && c4fLanguageIsArabic());
    assert(!strcmp(c4fTr("Red"), "أحمر"));
    systemLanguage = 21;
    c4fLanguageUse(C4F_LANGUAGE_ENGLISH);
    assert(!shownWith && !rtl && !c4fLanguageIsArabic());
    c4fLanguageUse(C4F_LANGUAGE_AUTO);
    assert(shownWith == c4fArabicText && rtl);
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
    puts("PASS automatic follows PS4 Portuguese/Arabic; explicit choices override it");
}

static void workerMessages(void)
{
    char english[160], out[C4F_MESSAGE_RENDER_SIZE];
    C4fMessage message;
    c4fMessageWrite(&message, english, sizeof(english),
        "Sent, but Control4Free did not start (%s). Retry Start.", "Left");
    c4fLanguageUse(C4F_LANGUAGE_PORTUGUESE);
    c4fMessageRender(&message, out, sizeof(out));
    assert(strstr(out, "(Left)") && !strstr(out, "Esquerdo"));
    c4fLanguageUse(C4F_LANGUAGE_ARABIC);
    c4fMessageRender(&message, out, sizeof(out));
    assert(strstr(out, "\xE2\x81\xA8Left\xE2\x81\xA9"));
    assert(!strcmp(english, "Sent, but Control4Free did not start (Left). Retry Start."));
    c4fMessageWrite(&message, english, sizeof(english), "The PS4 refused that (0x%08x).", 0x80020001u);
    c4fMessageRender(&message, out, sizeof(out));
    assert(strstr(out, "0x80020001"));
    c4fLanguageUse(C4F_LANGUAGE_ENGLISH);
    c4fMessageRender(&message, out, sizeof(out));
    assert(!strcmp(out, english));
    puts("PASS worker messages: language switching, literal values, Arabic isolation, English diagnostics");
}

int main(void)
{
    table();
    formattedKeys();
    arabicTable();
    choice();
    workerMessages();
    return 0;
}
