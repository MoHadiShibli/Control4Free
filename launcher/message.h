#ifndef C4F_LAUNCHER_MESSAGE_H
#define C4F_LAUNCHER_MESSAGE_H
/* Worker messages retain their source key and one typed argument. Their English
 * copy remains useful in logs; the renderer translates the key on every draw. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

enum { C4F_MESSAGE_NONE, C4F_MESSAGE_INT, C4F_MESSAGE_UINT, C4F_MESSAGE_STRING };
enum { C4F_MESSAGE_RENDER_SIZE = 1024 };
typedef struct {
    const char *key, *prefix;
    int argType, intValue, valueIsKey;
    unsigned uintValue;
    char value[256];
} C4fMessage;

/* This deliberately handles the launcher's existing single-argument templates,
 * rather than trying to recover a message key from already formatted text. */
static inline void c4fMessageWrite(C4fMessage *message, char *english, size_t size,
                                   const char *key, ...)
{
    memset(message, 0, sizeof(*message));
    message->key = key;
    va_list args, copy;
    va_start(args, key);
    va_copy(copy, args);
    if (english && size) vsnprintf(english, size, key, args);
    va_end(args);
    const char *conversion = strchr(key, '%');
    if (conversion) {
        ++conversion;
        while (*conversion && strchr("0123456789.-+ #", *conversion)) ++conversion;
        if (*conversion == 's') {
            message->argType = C4F_MESSAGE_STRING;
            const char *value = va_arg(copy, const char *);
            snprintf(message->value, sizeof(message->value), "%s", value ? value : "");
        } else if (*conversion == 'd') {
            message->argType = C4F_MESSAGE_INT;
            message->intValue = va_arg(copy, int);
        } else if (*conversion == 'x' || *conversion == 'X' || *conversion == 'u') {
            message->argType = C4F_MESSAGE_UINT;
            message->uintValue = va_arg(copy, unsigned);
        }
    }
    va_end(copy);
}

static inline void c4fMessageClear(C4fMessage *message)
{
    memset(message, 0, sizeof(*message));
}

#ifdef __cplusplus
extern "C" {
#endif
void c4fMessageRender(const C4fMessage *message, char *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
