#include "RHI/NativeWindow.h"

#include "SDL3/SDL.h"

#if USE_VULKAN
    #include "SDL3/SDL_vulkan.h"
#endif

namespace ya
{

SDLNativeWindow::~SDLNativeWindow()
{
    YA_CORE_INFO("SDLNativeWindow::~SDLNativeWindow()");
    destroy();
}

bool SDLNativeWindow::init()
{
    YA_CORE_INFO("SDLNativeWindow::init()");
    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool SDLNativeWindow::recreate(const WindowCreateInfo &ci)
{
    dpiScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    YA_CORE_INFO("system scale: {}, ci scale: {}, input size: {}x{}", dpiScale, ci.scale, ci.width, ci.height);

    int flags = 0;
    switch (ci.renderAPI) {
    case ERenderAPI::Vulkan: flags |= SDL_WINDOW_VULKAN; break;
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX: UNREACHABLE(); break;
    }
    if (ci.bResizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window *window = SDL_CreateWindow(ci.title.c_str(), static_cast<int>(ci.width), static_cast<int>(ci.height), flags);
    YA_CORE_ASSERT(window, "Failed to create window: {}", SDL_GetError());
    nativeWindowHandle = window;
    return true;
}

void SDLNativeWindow::destroy()
{
    YA_CORE_INFO("SDLNativeWindow::destroy()");
    if (nativeWindowHandle) {
        SDL_DestroyWindow(static_cast<SDL_Window *>(nativeWindowHandle));
        nativeWindowHandle = nullptr;
    }
}

void SDLNativeWindow::setTitle(const std::string &title)
{
    if (nativeWindowHandle) {
        SDL_SetWindowTitle(static_cast<SDL_Window *>(nativeWindowHandle), title.c_str());
    }
}

uint32_t SDLNativeWindow::getWindowID() const
{
    return nativeWindowHandle ? SDL_GetWindowID(static_cast<SDL_Window *>(nativeWindowHandle)) : 0;
}

void SDLNativeWindow::getWindowSize(int &width, int &height)
{
    SDL_GetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), &width, &height);
}

bool SDLNativeWindow::setWindowSize(int width, int height)
{
    if (nativeWindowHandle) {
        SDL_SetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), width, height);
        return true;
    }
    YA_CORE_ERROR("Failed to set window size: native window handle is null.");
    return false;
}

#if USE_VULKAN
bool SDLNativeWindow::onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface)
{
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(nativeWindowHandle),
                                  instance,
                                  nullptr,
                                  surface))
    {
        YA_CORE_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
        return false;
    }
    YA_CORE_INFO("Vulkan surface created successfully.");
    return true;
}

void SDLNativeWindow::onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface)
{
    SDL_Vulkan_DestroySurface(instance, *surface, nullptr);
    YA_CORE_INFO("Vulkan surface destroyed successfully.");
}

std::vector<const char *> SDLNativeWindow::onGetVkInstanceExtensions()
{
    Uint32 count = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions) {
        YA_CORE_ERROR("Failed to get Vulkan instance extensions: {}", SDL_GetError());
        return {};
    }
    return std::vector<const char *>(extensions, extensions + count);
}
#endif
} // namespace ya
