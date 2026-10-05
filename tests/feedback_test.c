/* c4fPadFeedbackParse against buffers read from the console (firmware 10.01). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c4f_vda.h"

static C4fPadFeedback parse(const uint8_t *bytes, size_t n)
{
    uint8_t buf[256] = {0};
    C4fPadFeedback f;
    memcpy(buf, bytes, n);
    c4fPadFeedbackParse(buf, sizeof(buf), &f);
    return f;
}

int main(void)
{
    /* A new controller, player 2: update flag, light bar 40 00 00. */
    static const uint8_t red[] = { 0x01, 0, 0, 0, 0, 0x40, 0, 0 };
    C4fPadFeedback f = parse(red, sizeof(red));
    assert(f.r == 0x40 && f.g == 0 && f.b == 0 && f.large == 0 && f.small == 0);

    /* Player 3, signed in: light bar 00 40 00, assigned flag at [16]. */
    static const uint8_t green[17] = { 0, 0, 0, 0, 0, 0, 0x40, 0, [16] = 1 };
    f = parse(green, sizeof(green));
    assert(f.r == 0 && f.g == 0x40 && f.b == 0);

    /* A game's hit: the small motor at full for ~300 ms. */
    static const uint8_t hit[] = { 0, 0, 0, 0xff, 0, 0, 0x40, 0 };
    f = parse(hit, sizeof(hit));
    assert(f.small == 0xff && f.large == 0 && f.g == 0x40);

    /* Both motors, by the DualShock 4 layout: [3] small, [4] large. */
    static const uint8_t both[] = { 0x01, 0, 0, 0x30, 0xc0, 0x20, 0, 0x20 };
    f = parse(both, sizeof(both));
    assert(f.small == 0x30 && f.large == 0xc0 && f.r == 0x20 && f.b == 0x20);

    /* Too short to hold the fields: nothing, rather than a misread. */
    uint8_t shortBuf[4] = { 1, 2, 3, 4 };
    c4fPadFeedbackParse(shortBuf, sizeof(shortBuf), &f);
    assert(!f.large && !f.small && !f.r && !f.g && !f.b);

    puts("PASS rumble and light bar read from the console's buffer layout");
}
