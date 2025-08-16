
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

    virtual ~WindowProvider()
    {
        NE_CORE_TRACE("WindowProvider::~WindowProvider()");
    }

    virtual bool init()    = 0;
    virtual void destroy() = 0;

    virtual void getWindowSize(int &width, int &height) = 0;
    virtual bool setWindowSize(int width, int height)
    {
        NE_CORE_ERROR("setWindowSize not implemented in WindowProvider");
        return false;
    }
};


#if USE_VULKAN
    #include "SDL3/SDL_vulkan.h"
#endif

class SDLWindowProvider : public WindowProvider
{
  public:
    SDLWindowProvider() = default;
    ~SDLWindowProvider() override
    {
        NE_CORE_INFO("SDLWindowProvider::~SDLWindowProvider()");
        if (nativeWindowHandle) {
            SDL_DestroyWindow(static_cast<SDL_Window *>(nativeWindowHandle));
            SDL_Quit();
            nativeWindowHandle = nullptr;
        }
    }

    bool init() override
    {
        NE_CORE_INFO("SDLWindowProvider::init()");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }

        // TODO: handle dpi
        float dpiScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

        SDL_Window *window = SDL_CreateWindow("ya", 1024, 768, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        NE_CORE_ASSERT(window, "Failed to create window: {}", SDL_GetError());
        nativeWindowHandle = window;

        return true;
    }

    void destroy() override
    {
        NE_CORE_INFO("SDLWindowProvider::destroy()");
        if (nativeWindowHandle) {
            SDL_DestroyWindow(static_cast<SDL_Window *>(nativeWindowHandle));
            nativeWindowHandle = nullptr;
        }
    }


    void getWindowSize(int &width, int &height) override
    {
        SDL_GetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), &width, &height);
    }
    bool setWindowSize(int width, int height) override
    {
        if (nativeWindowHandle) {
            SDL_SetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), width, height);
            return true;
        }
        NE_CORE_ERROR("Failed to set window size: native window handle is null.");
        return false;
    }

#if USE_VULKAN
    bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface)
    {
        if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(nativeWindowHandle),
                                      instance,
                                      nullptr, // if needed
                                      surface))
        {
            NE_CORE_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
            return false;
        }
        NE_CORE_INFO("Vulkan surface created successfully.");
        return true;
    }

    void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface)
    {
        SDL_Vulkan_DestroySurface(instance, *surface, nullptr); // if needed
        NE_CORE_INFO("Vulkan surface destroyed successfully.");
    }

    std::vector<const char *> onGetVkInstanceExtensions()
    {
        Uint32 count = 0;
        // VK_KHR_win32_surface
        const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!extensions) {
            NE_CORE_ERROR("Failed to get Vulkan instance extensions: {}", SDL_GetError());
            return {};
        }
        return std::vector<const char *>(extensions, extensions + count);
    }
#endif
};