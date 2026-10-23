#ifndef C4F_LAUNCHER_LANGUAGE_H
#define C4F_LAUNCHER_LANGUAGE_H
/* The app's language. Every text is written in English; in Brazilian
 * Portuguese, draw.c shows each one's translation (language.c) instead. The
 * choice (C4F_LANGUAGE_* in screen.h) is kept in /data, so it needs the app
 * out of the sandbox. */
#ifdef __cplusplus
extern "C" {
#endif

/* The saved choice; C4F_LANGUAGE_AUTO when there is none. */
int c4fLanguageLoad(void);
void c4fLanguageSave(int choice);
/* Shows every text in the language chosen. Automatic follows the PS4's system
 * language. */
void c4fLanguageUse(int choice);
/* A text in Brazilian Portuguese, or NULL when it has no translation. */
const char *c4fPortugueseText(const char *text);

#ifdef __cplusplus
}
#endif
#endif
