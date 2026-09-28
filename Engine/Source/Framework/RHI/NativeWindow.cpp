#include "RHI/NativeWindow.h"

#include "Core/System/VirtualFileSystem.h"
#include "SDL3/SDL.h"

#include <filesystem>

#if USE_VULKAN
    #include "SDL3/SDL_vulkan.h"
#endif

namespace ya
{

namespace
{

std::string& processIconPathStorage()
{
    static std::string path{kDefaultWindowIconPath};
    return path;
}

std::filesystem::path resolveWindowIconFile(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path input(path);
    if (std::filesystem::is_regular_file(input, ec)) {
        return input;
    }
    if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
        const std::filesystem::path translated = vfs->translatePath(path);
        if (std::filesystem::is_regular_file(translated, ec)) {
            return translated;
        }
    }
    return {};
}

bool applyWindowIcon(SDL_Window* window, const std::string& path)
{
    if (!window || path.empty()) {
        return false;
    }
    const std::filesystem::path file = resolveWindowIconFile(path);
    if (file.empty()) {
        YA_CORE_WARN("Window icon not found: {}", path);
        return false;
    }

    const std::string ext = file.extension().string();
    SDL_Surface*      loaded = nullptr;
    if (ext == ".bmp" || ext == ".BMP") {
        loaded = SDL_LoadBMP(file.string().c_str());
    }
    else {
        loaded = SDL_LoadPNG(file.string().c_str());
    }
    if (!loaded) {
        YA_CORE_WARN("Failed to load window icon '{}': {}", file.string(), SDL_GetError());
        return false;
    }

    SDL_Surface* icon = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!icon) {
        YA_CORE_WARN("Failed to convert window icon '{}': {}", file.string(), SDL_GetError());
        return false;
    }

    const bool ok = SDL_SetWindowIcon(window, icon);
    if (!ok) {
        YA_CORE_WARN("Failed to set window icon: {}", SDL_GetError());
    }
    SDL_DestroySurface(icon);
    return ok;
}

} // namespace

void setProcessWindowIconPath(std::string path)
{
    if (path.empty()) {
        processIconPathStorage() = std::string(kDefaultWindowIconPath);
        return;
    }
    processIconPathStorage() = std::move(path);
}

const std::string& processWindowIconPath()
{
    return processIconPathStorage();
}

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
    if (ci.bBorderless) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (ci.bAlwaysOnTop) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if (ci.bTransparent) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    if (ci.bNotFocusable) {
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    if (ci.bUtility) {
        flags |= SDL_WINDOW_UTILITY;
    }
    if (ci.bHidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    SDL_Window *window = SDL_CreateWindow(ci.title.c_str(), static_cast<int>(ci.width), static_cast<int>(ci.height), flags);
    if (!window) {
        YA_CORE_ERROR("Failed to create window: {}", SDL_GetError());
        return false;
    }
    nativeWindowHandle = window;
    // Per-window content scale (== device pixel ratio). MUST be queried AFTER
    // creation: the scale belongs to the window's display, so asking before the
    // window exists always answered 1.0 and the whole app then rendered at 1x
    // on a 2x display (macOS upscaled the frame -> everything, text and edges
    // alike, came out soft). Use the window's own display rather than the
    // primary one, or a window on a secondary HiDPI monitor inherits the
    // primary's scale.
    refreshDpiScale();
    {
        int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
        SDL_GetWindowSize(window, &logicalW, &logicalH);
        SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
        YA_CORE_INFO("window created: scale {}, logical {}x{}, drawable {}x{}, requested {}x{}",
                     dpiScale, logicalW, logicalH, pixelW, pixelH, ci.width, ci.height);
    }
    if (ci.bAlwaysOnTop) {
        (void)SDL_SetWindowAlwaysOnTop(window, true);
    }
    const std::string& iconPath = ci.iconPath.empty() ? processWindowIconPath() : ci.iconPath;
    (void)applyWindowIcon(window, iconPath);
    return true;
}

void SDLNativeWindow::destroy()
{
    YA_CORE_INFO("SDLNativeWindow::destroy()");
    if (nativeWindowHandle) {
        (void)setHitTest(nullptr, nullptr);
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

bool SDLNativeWindow::setIcon(const std::string& path)
{
    if (!nativeWindowHandle) {
        return false;
    }
    const std::string& iconPath = path.empty() ? processWindowIconPath() : path;
    return applyWindowIcon(static_cast<SDL_Window *>(nativeWindowHandle), iconPath);
}

uint32_t SDLNativeWindow::getWindowID() const
{
    return nativeWindowHandle ? SDL_GetWindowID(static_cast<SDL_Window *>(nativeWindowHandle)) : 0;
}

void SDLNativeWindow::getWindowSize(int &width, int &height)
{
    SDL_GetWindowSize(static_cast<SDL_Window *>(nativeWindowHandle), &width, &height);
}

bool SDLNativeWindow::isMinimized() const
{
    if (!nativeWindowHandle) {
        return false;
    }
    return (SDL_GetWindowFlags(static_cast<SDL_Window *>(nativeWindowHandle)) & SDL_WINDOW_MINIMIZED) != 0;
}

bool SDLNativeWindow::minimize()
{
    if (!nativeWindowHandle) {
        return false;
    }
    if (!SDL_MinimizeWindow(static_cast<SDL_Window *>(nativeWindowHandle))) {
        return false;
    }
    SDL_PumpEvents();
    return true;
}

bool SDLNativeWindow::restoreFromMinimize()
{
    if (!nativeWindowHandle) {
        return false;
    }
    if (!SDL_RestoreWindow(static_cast<SDL_Window *>(nativeWindowHandle))) {
        return false;
    }
    SDL_PumpEvents();
    return true;
}

bool SDLNativeWindow::isHidden() const
{
    if (!nativeWindowHandle) {
        return false;
    }
    return (SDL_GetWindowFlags(static_cast<SDL_Window *>(nativeWindowHandle)) & SDL_WINDOW_HIDDEN) != 0;
}

bool SDLNativeWindow::hide()
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_HideWindow(static_cast<SDL_Window *>(nativeWindowHandle));
}

bool SDLNativeWindow::show()
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_ShowWindow(static_cast<SDL_Window *>(nativeWindowHandle));
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

bool SDLNativeWindow::getWindowPosition(int& x, int& y) const
{
    x = 0;
    y = 0;
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_GetWindowPosition(static_cast<SDL_Window *>(nativeWindowHandle), &x, &y);
}

bool SDLNativeWindow::setWindowPosition(int x, int y)
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_SetWindowPosition(static_cast<SDL_Window *>(nativeWindowHandle), x, y);
}

int SDLNativeWindow::getDisplayIndex() const
{
    if (!nativeWindowHandle) {
        return -1;
    }
    const SDL_DisplayID current = SDL_GetDisplayForWindow(static_cast<SDL_Window *>(nativeWindowHandle));
    if (current == 0) {
        return -1;
    }
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays) {
        return -1;
    }
    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (displays[i] == current) {
            index = i;
            break;
        }
    }
    SDL_free(displays);
    return index;
}

std::string SDLNativeWindow::getDisplayName() const
{
    if (!nativeWindowHandle) {
        return {};
    }
    const SDL_DisplayID current = SDL_GetDisplayForWindow(static_cast<SDL_Window *>(nativeWindowHandle));
    if (current == 0) {
        return {};
    }
    const char* name = SDL_GetDisplayName(current);
    return name ? std::string(name) : std::string{};
}

bool SDLNativeWindow::isMaximized() const
{
    if (!nativeWindowHandle) {
        return false;
    }
    return (SDL_GetWindowFlags(static_cast<SDL_Window *>(nativeWindowHandle)) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool SDLNativeWindow::maximize()
{
    if (!nativeWindowHandle) {
        return false;
    }
    if (!SDL_MaximizeWindow(static_cast<SDL_Window *>(nativeWindowHandle))) {
        return false;
    }
    SDL_PumpEvents();
    return true;
}

bool SDLNativeWindow::restoreFromMaximize()
{
    if (!nativeWindowHandle) {
        return false;
    }
    if (!SDL_RestoreWindow(static_cast<SDL_Window *>(nativeWindowHandle))) {
        return false;
    }
    SDL_PumpEvents();
    return true;
}

NativeWindowSafeArea SDLNativeWindow::getSafeArea() const
{
    NativeWindowSafeArea area;
    if (!nativeWindowHandle) {
        return area;
    }
    SDL_Rect rect{};
    if (!SDL_GetWindowSafeArea(static_cast<SDL_Window *>(nativeWindowHandle), &rect)) {
        return area;
    }
    area.x     = rect.x;
    area.y     = rect.y;
    area.w     = rect.w;
    area.h     = rect.h;
    area.valid = true;
    return area;
}

bool SDLNativeWindow::getBordersSize(int& top, int& left, int& bottom, int& right) const
{
    top = left = bottom = right = 0;
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_GetWindowBordersSize(static_cast<SDL_Window *>(nativeWindowHandle), &top, &left, &bottom, &right);
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

bool SDLNativeWindow::setGlobalMouseCapture(bool capture)
{
    return SDL_CaptureMouse(capture);
}

bool SDLNativeWindow::setBordered(bool bordered)
{
    if (!nativeWindowHandle) {
        return false;
    }
    return SDL_SetWindowBordered(static_cast<SDL_Window *>(nativeWindowHandle), bordered);
}

bool SDLNativeWindow::setMousePassthrough(bool enable)
{
    (void)enable;
    // SDL 3.4 has no mouse-passthrough window API. Click-through stays on
    // INativeWindow so backends/OS chrome can implement it; macOS uses AppKit.
    return false;
}

namespace
{

SDL_HitTestResult toSdlHitTest(ENativeWindowHitResult hit)
{
    switch (hit) {
    case ENativeWindowHitResult::Draggable: {
        return SDL_HITTEST_DRAGGABLE;
    }
    case ENativeWindowHitResult::ResizeTop: {
        return SDL_HITTEST_RESIZE_TOP;
    }
    case ENativeWindowHitResult::ResizeBottom: {
        return SDL_HITTEST_RESIZE_BOTTOM;
    }
    case ENativeWindowHitResult::ResizeLeft: {
        return SDL_HITTEST_RESIZE_LEFT;
    }
    case ENativeWindowHitResult::ResizeRight: {
        return SDL_HITTEST_RESIZE_RIGHT;
    }
    case ENativeWindowHitResult::ResizeTopLeft: {
        return SDL_HITTEST_RESIZE_TOPLEFT;
    }
    case ENativeWindowHitResult::ResizeTopRight: {
        return SDL_HITTEST_RESIZE_TOPRIGHT;
    }
    case ENativeWindowHitResult::ResizeBottomLeft: {
        return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    }
    case ENativeWindowHitResult::ResizeBottomRight: {
        return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    case ENativeWindowHitResult::Normal: {
        return SDL_HITTEST_NORMAL;
    }
    }
    return SDL_HITTEST_NORMAL;
}

SDL_HitTestResult SDLCALL dispatchNativeWindowHitTest(SDL_Window* /*window*/,
                                                      const SDL_Point* area,
                                                      void* userdata)
{
    auto* self = static_cast<SDLNativeWindow*>(userdata);
    if (!self || !area) {
        return SDL_HITTEST_NORMAL;
    }
    return toSdlHitTest(self->invokeHitTest(static_cast<float>(area->x), static_cast<float>(area->y)));
}

} // namespace

bool SDLNativeWindow::setHitTest(NativeWindowHitTestFn fn, void* userdata)
{
    _hitTest         = fn;
    _hitTestUserdata = userdata;
    if (!nativeWindowHandle) {
        return false;
    }
    auto* window = static_cast<SDL_Window *>(nativeWindowHandle);
    if (!fn) {
        return SDL_SetWindowHitTest(window, nullptr, nullptr);
    }
    return SDL_SetWindowHitTest(window, dispatchNativeWindowHitTest, this);
}

ENativeWindowHitResult SDLNativeWindow::invokeHitTest(float x, float y) const
{
    if (!_hitTest) {
        return ENativeWindowHitResult::Normal;
    }
    return _hitTest(_hitTestUserdata, x, y);
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
