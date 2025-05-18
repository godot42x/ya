
#pragma once

#include "Core/Log.h"
#include "SDL3/SDL.h"

class WindowProvider
{

  protected:
    void *nativeWindowHandle = nullptr;

  public:

    void *getNativeWindowPtr() { return nativeWindowHandle; }
    template <typename T>
    T *getNativeWindowPtr() { return static_cast<T *>(nativeWindowHandle); }

    virtual ~WindowProvider() = default;

    virtual bool init() = 0;

    virtual void getWindowSize(int &width, int &height) = 0;
};



class SDLWindowProvider : public WindowProvider
{
  public:
    SDLWindowProvider() = default;
    ~SDLWindowProvider() override;

    bool init() override
    {
        NE_CORE_INFO("SDLDevice::init()");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }

        SDL_Window *window = SDL_CreateWindow("Neon", 1024, 768, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        NE_CORE_ASSERT(window, "Failed to create window: {}", SDL_GetError());
        nativeWindowHandle = window;

        return true;
    }

    void getWindowSize(int &width, int &height) override
    {
        SDL_GetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), &width, &height);
    }
};