
#pragma once

#include "Core/Log.h"
#include "Render/Render.h"
#include "SDL3/SDL.h"

#if USE_VULKAN
// Forward declarations for Vulkan types (avoid including vulkan headers in this public interface)
typedef struct VkInstance_T   *VkInstance;
typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
#endif

namespace ya
{


struct WindowCreateInfo
{
    uint32_t      index      = 0;
    ERenderAPI::T renderAPI  = ERenderAPI::None;
    std::string   title      = "Window Title";
    uint32_t      width      = 1024;
    uint32_t      height     = 768;
    float         scale      = 1.0f;
    bool          bResizable = true;
};

struct IWindowProvider
{

  protected:
    void *nativeWindowHandle = nullptr;
    float dpiScale           = 1.0f; // DPI scale factor, default is 1.0

  public:

    void *getNativeWindowPtr() { return nativeWindowHandle; }
    template <typename T>
    T *getNativeWindowPtr() { return static_cast<T *>(nativeWindowHandle); }

    virtual ~IWindowProvider()
    {
        YA_CORE_TRACE("WindowProvider::~WindowProvider()");
    }

    // TODO: support multiple windows
    virtual bool init()                               = 0;
    virtual void destroy()                            = 0;
    virtual bool recreate(const WindowCreateInfo &ci) = 0;

    void getWindowSize(float &width, float &height)
    {
        int w = 0, h = 0;
        getWindowSize(w, h);
        width  = static_cast<float>(w);
        height = static_cast<float>(h);
    }
    virtual void getWindowSize(int &width, int &height) = 0;
    virtual bool setWindowSize(int width, int height)
    {
        YA_CORE_ERROR("setWindowSize not implemented in IWindowProvider");
        return false;
    }
};



class SDLWindowProvider : public IWindowProvider
{

  public:
    SDLWindowProvider() = default;
    ~SDLWindowProvider() override
    {
        YA_CORE_INFO("SDLWindowProvider::~SDLWindowProvider()");
        if (nativeWindowHandle) {
            SDL_DestroyWindow(static_cast<SDL_Window *>(nativeWindowHandle));
            SDL_Quit();
            nativeWindowHandle = nullptr;
        }
    }

    bool init() override
    {
        YA_CORE_INFO("SDLWindowProvider::init()");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }
        return true;
    }
    bool recreate(const WindowCreateInfo &ci) override
    {
        // TODO: handle dpi
        dpiScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        YA_CORE_INFO("system scale: {}, ci scale: {}, input size: {}x{}", dpiScale, ci.scale, ci.width, ci.height);
        // double scale = dpiScale * ci.scale;
        int w = (int)(ci.width);
        int h = (int)(ci.height);

        int flags = 0;
        switch (ci.renderAPI) {
        case ERenderAPI::Vulkan:
            flags |= SDL_WINDOW_VULKAN;
            break;
        case ERenderAPI::None:
        case ERenderAPI::OpenGL:
        case ERenderAPI::DirectX12:
        case ERenderAPI::Metal:
        case ERenderAPI::ENUM_MAX:
            UNREACHABLE();
            break;
        }
        if (ci.bResizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }

        SDL_Window *window = SDL_CreateWindow(ci.title.c_str(), w, h, flags);
        YA_CORE_ASSERT(window, "Failed to create window: {}", SDL_GetError());
        nativeWindowHandle = window;

        return true;
    }

    void destroy() override
    {
        YA_CORE_INFO("SDLWindowProvider::destroy()");
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
        YA_CORE_ERROR("Failed to set window size: native window handle is null.");
        return false;
    }

#if USE_VULKAN
    bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface);

    void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface);

    std::vector<const char *> onGetVkInstanceExtensions();
#endif
};
} // namespace ya