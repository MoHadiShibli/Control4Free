#ifndef C4F_LAUNCHER_LANGUAGE_H
#define C4F_LAUNCHER_LANGUAGE_H
/* Offline interface catalogs. Translate a source message key before formatting
 * values into it. Saved language choices require the app to leave its sandbox. */
#ifdef __cplusplus
extern "C" {
#endif

/* The saved choice; C4F_LANGUAGE_AUTO when there is none. */
int c4fLanguageLoad(void);
void c4fLanguageSave(int choice);
/* Shows every text in the language chosen. Automatic follows the PS4's system
 * language. */
void c4fLanguageUse(int choice);
/* Exact source-key lookups, returning NULL when a catalog has no entry. */
const char *c4fPortugueseText(const char *key);
const char *c4fArabicText(const char *key);
/* Active-language lookup; an unknown key is returned unchanged. */
const char *c4fTr(const char *key);
int c4fLanguageIsArabic(void);

#ifdef __cplusplus
}
#endif
#endif
