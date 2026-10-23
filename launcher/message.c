#include "message.h"
#include "language.h"

void c4fMessageRender(const C4fMessage *message, char *out, size_t size)
{
    if (!out || !size) return;
    out[0] = 0;
    if (!message || !message->key) return;
    const char *key = c4fTr(message->key);
    char body[C4F_MESSAGE_RENDER_SIZE];
    switch (message->argType) {
    case C4F_MESSAGE_INT:
        snprintf(body, sizeof(body), key, message->intValue);
        break;
    case C4F_MESSAGE_UINT:
        snprintf(body, sizeof(body), key, message->uintValue);
        break;
    case C4F_MESSAGE_STRING: {
        const char *value = message->valueIsKey ? c4fTr(message->value) : message->value;
        char isolated[sizeof(message->value) * 3 + 8];
        if (c4fLanguageIsArabic() && !message->valueIsKey) {
            snprintf(isolated, sizeof(isolated), "\xE2\x81\xA8%s\xE2\x81\xA9", value);
            value = isolated;
        }
        snprintf(body, sizeof(body), key, value);
        break;
    }
    default:
        snprintf(body, sizeof(body), "%s", key);
        break;
    }
    if (message->prefix) snprintf(out, size, c4fTr(message->prefix), body);
    else snprintf(out, size, "%s", body);
}
