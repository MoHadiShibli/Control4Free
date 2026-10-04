#ifndef C4F_LAUNCHER_LOGO_H
#define C4F_LAUNCHER_LOGO_H
/* The Control4Free mark: a DualShock 4 seen from above, in a 1000 x 640
 * design box (y down). The page's app mark, toast icon and favicon use the
 * same path: print it with `build/launcher/launcher-art svg` after a change. */
#include "draw.h"

#define C4F_LOGO_CENTER_X 500.0f
#define C4F_LOGO_CENTER_Y 320.0f
#define C4F_LOGO_WIDTH 880.0f /* body width in design units */

/* cutouts: 0 for the bare outline, 1 with the buttons, touchpad and sticks. */
void c4fLogoPath(C4fPath *p, int cutouts);
/* The light bar along the top edge, for an LED colour. */
void c4fLogoLightPath(C4fPath *p);

/* A placement that centres the controller at x,y, `width` pixels wide. */
C4fPlacement c4fLogoAt(float x, float y, float width, float angle);

#endif
