#ifndef C4F_LAUNCHER_AUTORUN_H
#define C4F_LAUNCHER_AUTORUN_H
/* GoldHEN AutoRun for the bundled payload: a copy in /data/payloads and an
 * entry in /data/GoldHEN/payloads.ini, the files GoldHEN's Payloader LaunchPad
 * uses. The app leaves its sandbox for this through GoldHEN's SDK call. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { C4F_AUTORUN_UNKNOWN = -1, C4F_AUTORUN_OFF = 0, C4F_AUTORUN_ON = 1, C4F_AUTORUN_OUTDATED = 2 };

/* Host tests point the data folder elsewhere; the console uses /user/data. */
void c4fAutorunSetRoot(const char *root);
/* Reads the bundled payload into memory; 0 on success. Do it before leaving
 * the sandbox: /app0 is only visible from inside. */
int c4fAutorunLoadBundled(const char *path);
const unsigned char *c4fAutorunBundled(size_t *size);
int c4fAutorunCheck(char *problem, size_t size);
int c4fAutorunEnable(char *message, size_t size);
int c4fAutorunDisable(char *message, size_t size);

#ifdef __cplusplus
}
#endif
#endif
