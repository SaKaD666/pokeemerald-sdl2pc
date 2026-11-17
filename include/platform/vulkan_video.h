#ifndef GUARD_PLATFORM_VULKAN_VIDEO_H
#define GUARD_PLATFORM_VULKAN_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

bool PlatformVulkan_Init(SDL_Window *window);
bool PlatformVulkan_RenderFrame(const uint16_t *pixels);
void PlatformVulkan_Shutdown(void);

#endif // GUARD_PLATFORM_VULKAN_VIDEO_H
