#ifndef C4F_LAUNCHER_TEXT_H
#define C4F_LAUNCHER_TEXT_H
#include "draw.h"
/* Internal Unicode layout path. Called only by the drawing thread. */
int c4fShapedTextInit(void);
int c4fTextNeedsShaping(const char *text, int length);
float c4fShapedText(C4fCanvas *c, int font, float size, float x, float y,
                    C4fColor color, const char *text, int length);
void c4fTextFontChanged(void);
#endif
