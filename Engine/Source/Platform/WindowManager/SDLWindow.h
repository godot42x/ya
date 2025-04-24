

#pragma once


#include "Core/Log.h"
#include "Render/WindowManager.h"
#include "SDL3/SDL_video.h"

namespace SDL
{



struct SDLWindow : public Window
{
    bool init()
    {
        SDL_Window *window = SDL_CreateWindow("Neon", 1024, 768, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        NE_CORE_ASSERT(window, "Failed to create SDL window: {}", SDL_GetError());
        nativeWindowHandle = window;

        return window != nullptr;
    }
};
} // namespace SDL