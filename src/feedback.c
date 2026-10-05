/* Control4Free -- what a game asks of a controller: rumble and light bar.
 *
 * Pure byte work, with no PS4 calls, so the host tests run the same code the
 * console does.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "c4f_vda.h"

/* scePadVirtualDeviceGetRemoteSetting fills a buffer laid out like the DualShock
 * 4's HID output report without its report ID. Read on firmware 10.01
 * (2026-10-05) while games rumbled and users signed in:
 *
 *   [0]  update flags: bit 0 rumble, bit 1 light bar, as in the DS4 report.
 *        Pulses for a few milliseconds, so it is not relied on here.
 *   [3]  right motor, the small fast one: 0xff for ~300 ms on a game's hit.
 *   [4]  left motor, the large slow one.
 *   [5..7]  light bar red, green, blue: 0x40 at full, as the PS4 sets them
 *        (40 00 00 for player 2, 00 40 00 for player 3).
 *   [16] 1 once a user is assigned.
 */
void c4fPadFeedbackParse(const uint8_t *buf, size_t size, C4fPadFeedback *out)
{
    (void)memset(out, 0, sizeof(*out));
    if (size < 8) return;
    out->small = buf[3];
    out->large = buf[4];
    out->r = buf[5];
    out->g = buf[6];
    out->b = buf[7];
}
