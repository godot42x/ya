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
    // Per-window content scale (== device pixel ratio on this display). Use
    // the window's own display, not the primary one — otherwise a window on a
    // secondary HiDPI monitor would wrongly inherit the primary's scale.
    refreshDpiScale();
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
    if (!window) {
        YA_CORE_ERROR("Failed to create window: {}", SDL_GetError());
        return false;
    }
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

void SDLNativeWindow::refreshDpiScale()
{
    if (!nativeWindowHandle) {
        dpiScale = 1.0f;
        return;
    }
    // SDL3: content scale == device pixel ratio for the window's current
    // display. Returns 1.0 on standard-DPI monitors, 2.0 / 1.5x on Retina etc.
    const float scale = SDL_GetWindowDisplayScale(static_cast<SDL_Window *>(nativeWindowHandle));
    dpiScale          = (scale > 0.0f) ? scale : 1.0f;
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

bool SDLNativeWindow::startTextInput()
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_StartTextInput(static_cast<SDL_Window *>(nativeWindowHandle));
}

bool SDLNativeWindow::stopTextInput()
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_StopTextInput(static_cast<SDL_Window *>(nativeWindowHandle));
}

bool SDLNativeWindow::setMouseGrab(bool grab)
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_SetWindowMouseGrab(static_cast<SDL_Window *>(nativeWindowHandle), grab);
}

bool SDLNativeWindow::setRelativeMouseMode(bool relative)
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window *>(nativeWindowHandle), relative);
}

bool SDLNativeWindow::setMouseConfineRect(const Rect2D* rect)
{
    if (!nativeWindowHandle) {
        return false;
    }
    auto* window = static_cast<SDL_Window *>(nativeWindowHandle);
    if (!rect) {
        return SDL_SetWindowMouseRect(window, nullptr);
    }
    const SDL_Rect sdlRect{
        .x = static_cast<int>(rect->pos.x),
        .y = static_cast<int>(rect->pos.y),
        .w = static_cast<int>(rect->extent.x),
        .h = static_cast<int>(rect->extent.y),
    };
    return SDL_SetWindowMouseRect(window, &sdlRect);
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
