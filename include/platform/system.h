#ifndef GUARD_SYSTEM_H
#define GUARD_SYSTEM_H

void RunDMAsAndVBlank(void);
void AudioUpdate(void);
#ifdef PLATFORM_SDL2
#define ENTER_VBLANK() (REG_VCOUNT = DISPLAY_HEIGHT)
#else
#define ENTER_VBLANK() (REG_VCOUNT = 161)
#endif
#endif
