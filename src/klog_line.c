/* Control4Free -- reading meaning out of a kernel-log line.
 *
 * Pure string work, with no PS4 calls, so the host tests run the same code the
 * console does.
 */

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "c4f_vda.h"

static uint64_t c4fParseHexAfter(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    uint64_t value = 0;

    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!isxdigit((unsigned char)*p)) return 0;

    while (isxdigit((unsigned char)*p)) {
        char c = *p++;
        value <<= 4;
        if (c >= '0' && c <= '9') value |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (uint64_t)(10 + c - 'a');
        else value |= (uint64_t)(10 + c - 'A');
    }
    return value;
}

uint64_t c4fKlogDeviceId(const char *line)
{
    static const char *keys[] = { "DeviceId", "DeviceID", "deviceId", "deviceID" };
    size_t i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        uint64_t id = c4fParseHexAfter(line, keys[i]);
        if (id) return id;
    }
    return 0;
}

/* The login manager's line for a new virtual pad, as logged on firmware 10.01:
 *
 *   #LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]
 *
 * subType 2 is the Remote Play pad and is the same every time. type has been 1
 * (created for user 1, which is what we do) and 4 (created for a local-user id),
 * so it is not part of the match. Nothing looser is accepted: a real pad being
 * plugged in, or any other MBus device appearing in the same window, would
 * otherwise hand us a DeviceId that is not ours, and every input would then go
 * to a device we do not own. */
int c4fKlogIsVirtualAdd(const char *line)
{
    return strstr(line, "SCE_MBUS_EVENT_DEVICE_ADDED") != NULL &&
           (strstr(line, "[subType:2]") != NULL || strstr(line, "subType=2") != NULL);
}
