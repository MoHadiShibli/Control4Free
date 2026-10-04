/* The DualShock 4 mark. The outline runs counter-clockwise on screen and the
 * cut-outs clockwise, so non-zero winding removes them; a stick cap inside
 * its cut ring runs counter-clockwise again and stays. */
#include "logo.h"

/* One half of the body, top centre to bottom centre down the left side:
 * cubic segments (c1x c1y c2x c2y x y). The right half mirrors it. */
static const float c4fBodyLeft[][6] = {
    { 410,  78, 330,  74, 262,  62 },   /* top edge out to the L1 shoulder */
    { 232,  46, 188,  42, 146,  62 },   /* over the shoulder */
    { 108,  80,  86, 120,  78, 170 },   /* round the upper corner */
    {  70, 230,  58, 320,  60, 400 },   /* down the outer side */
    {  62, 470,  80, 535, 118, 568 },   /* along the grip */
    { 152, 598, 196, 592, 222, 560 },   /* round the grip's end */
    { 250, 525, 272, 470, 300, 430 },   /* up its inner edge */
    { 360, 418, 430, 440, 500, 440 },   /* under the sticks to the centre */
};
#define C4F_BODY_START_X 500.0f
#define C4F_BODY_START_Y 78.0f

void c4fLogoPath(C4fPath *p, int cutouts)
{
    const int n = (int)(sizeof(c4fBodyLeft) / sizeof(c4fBodyLeft[0]));
    c4fPathMove(p, C4F_BODY_START_X, C4F_BODY_START_Y);
    for (int i = 0; i < n; i++) {
        const float *s = c4fBodyLeft[i];
        c4fPathCubic(p, s[0], s[1], s[2], s[3], s[4], s[5]);
    }
    /* Back up the right side: each left segment mirrored and reversed. */
    for (int i = n - 1; i >= 0; i--) {
        const float *s = c4fBodyLeft[i];
        float startX = i ? c4fBodyLeft[i - 1][4] : C4F_BODY_START_X, startY = i ? c4fBodyLeft[i - 1][5] : C4F_BODY_START_Y;
        c4fPathCubic(p, 1000 - s[2], s[3], 1000 - s[0], s[1], 1000 - startX, startY);
    }
    if (!cutouts) return;

    /* Touchpad. */
    c4fPathRoundRect(p, 340, 98, 320, 154, 26, 1);
    /* D-pad. */
    {
        const float cx = 196, cy = 212, a = 23, l = 64;
        c4fPathMove(p, cx - a, cy - l);
        c4fPathLine(p, cx + a, cy - l);
        c4fPathLine(p, cx + a, cy - a);
        c4fPathLine(p, cx + l, cy - a);
        c4fPathLine(p, cx + l, cy + a);
        c4fPathLine(p, cx + a, cy + a);
        c4fPathLine(p, cx + a, cy + l);
        c4fPathLine(p, cx - a, cy + l);
        c4fPathLine(p, cx - a, cy + a);
        c4fPathLine(p, cx - l, cy + a);
        c4fPathLine(p, cx - l, cy - a);
        c4fPathLine(p, cx - a, cy - a);
        c4fPathLine(p, cx - a, cy - l);
    }
    /* Face buttons. */
    c4fPathCircle(p, 804, 148, 27, 1);
    c4fPathCircle(p, 804, 276, 27, 1);
    c4fPathCircle(p, 740, 212, 27, 1);
    c4fPathCircle(p, 868, 212, 27, 1);
    /* Sticks: a cut ring around a standing cap. */
    c4fPathCircle(p, 338, 342, 66, 1);
    c4fPathCircle(p, 338, 342, 44, 0);
    c4fPathCircle(p, 662, 342, 66, 1);
    c4fPathCircle(p, 662, 342, 44, 0);
    /* PS button. */
    c4fPathCircle(p, 500, 336, 17, 1);
}

void c4fLogoLightPath(C4fPath *p)
{
    /* Just above the touchpad, where the light bar shows from above. */
    c4fPathRoundRect(p, 350, 78, 300, 17, 8.5f, 0);
}

C4fPlacement c4fLogoAt(float x, float y, float width, float angle)
{
    C4fPlacement at = { C4F_LOGO_CENTER_X, C4F_LOGO_CENTER_Y, width / C4F_LOGO_WIDTH, angle, x, y };
    return at;
}
