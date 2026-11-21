#ifndef GUARD_FRAMEDRAW_H
#define GUARD_FRAMEDRAW_H

#include "global.h"

void DrawFrame(uint16_t *pixels);
#ifdef PLATFORM_SDL2
void SetFrameViewToGbaSize(bool8 enabled);
#endif
#endif