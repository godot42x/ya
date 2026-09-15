
#pragma once

#include "Core/Log.h"
#include "Core/Common/Types.h"
#include "RHI/Render.h"

#include <string>
#include <string_view>
#include <vector>

#if USE_VULKAN
// Forward declarations for Vulkan types (avoid including vulkan headers in this public interface)
typedef struct VkInstance_T   *VkInstance;
typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
#endif

namespace ya
{

/// Default Dock / taskbar icon. Games override this from `.yaproject` `icon`.
inline constexpr std::string_view kDefaultWindowIconPath = "Engine/Content/Branding/ya-icon.png";

/// Process-wide icon used when `WindowCreateInfo::iconPath` is empty.
/// Empty `path` restores `kDefaultWindowIconPath`.
YA_RHI_API void setProcessWindowIconPath(std::string path);
[[nodiscard]] YA_RHI_API const std::string& processWindowIconPath();

struct WindowCreateInfo
{
    uint32_t      index      = 0;
    ERenderAPI::T renderAPI  = ERenderAPI::None;
    std::string   title      = "Window Title";
    uint32_t      width      = 1024;
    uint32_t      height     = 768;
    float         scale       = 1.0f;
    bool          bResizable  = true;
    bool          bBorderless = false;
    bool          bAlwaysOnTop = false;
    bool          bTransparent = false;
    bool          bNotFocusable = false;
    bool          bUtility = false;
    bool          bMousePassthrough = false;
    /// Empty = `processWindowIconPath()` (YA branding, or the packed game icon).
    std::string   iconPath;
};

struct NativeWindowSafeArea
{
    int  x     = 0;
    int  y     = 0;
    int  w     = 0;
    int  h     = 0;
    bool valid = false;
};

/// Non-client hit result in window coordinates (top-left origin).
/// SDL maps these onto `SDL_HitTestResult`; GUI chrome classifies first.
enum class ENativeWindowHitResult : uint8_t
{
    Normal,
    Draggable,
    ResizeTop,
    ResizeBottom,
    ResizeLeft,
    ResizeRight,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
};

using NativeWindowHitTestFn = ENativeWindowHitResult (*)(void* userdata, float x, float y);

/// One native top-level window plus the backend surface hooks bound to it.
/// App/window policy belongs to higher-level host code; this interface only
/// represents the concrete native window object.
struct INativeWindow
{
  protected:
    void *nativeWindowHandle = nullptr;
    float dpiScale           = 1.0f; // DPI scale factor, default is 1.0

  public:
    virtual ~INativeWindow()
    {
        YA_CORE_TRACE("INativeWindow::~INativeWindow()");
    }

    [[nodiscard]] void *getNativeWindowHandle() const { return nativeWindowHandle; }

    /// Device pixel ratio (content scale) of the display the window lives on.
    /// Set by the backend at create/resize time from the window-system DPI,
    /// NOT derived from present/logical extent — that avoids the classic
    /// "DPI assumed 1.0" pitfall on HiDPI monitors. Defaults to 1.0 when no
    /// native window exists (headless/scenario).
    [[nodiscard]] float getDpiScale() const { return dpiScale; }

    // TODO: support multiple windows
    virtual bool init()                               = 0;
    virtual void destroy()                            = 0;
    virtual bool recreate(const WindowCreateInfo &ci) = 0;
    virtual void setTitle(const std::string &title)    = 0;
    /// PNG or BMP. Empty path applies `processWindowIconPath()`. Missing file
    /// is a warning, not a window-create failure.
    virtual bool setIcon(const std::string& path)
    {
        (void)path;
        return false;
    }
    [[nodiscard]] virtual uint32_t getWindowID() const = 0;

    /// Re-read per-monitor DPI. Default no-op when the backend has no display.
    virtual void refreshDpiScale() {}

    virtual bool startTextInput() { return false; }
    virtual bool stopTextInput() { return false; }
    virtual bool setMouseGrab(bool grab)
    {
        (void)grab;
        return false;
    }
    virtual bool setRelativeMouseMode(bool relative)
    {
        (void)relative;
        return false;
    }
    /// Confine the cursor to `rect` in window coordinates. Pass nullptr to clear.
    virtual bool setMouseConfineRect(const Rect2D* rect)
    {
        (void)rect;
        return false;
    }
    /// Continue receiving mouse moves after the cursor leaves this window
    /// (Win32 SetCapture / SDL_CaptureMouse). Does not grab or confine the cursor.
    virtual bool setGlobalMouseCapture(bool capture)
    {
        (void)capture;
        return false;
    }
    virtual bool setBordered(bool bordered)
    {
        (void)bordered;
        return false;
    }
    virtual bool setMousePassthrough(bool enable)
    {
        (void)enable;
        return false;
    }
    /// Pass `fn == nullptr` to restore default client hits.
    virtual bool setHitTest(NativeWindowHitTestFn fn, void* userdata)
    {
        (void)fn;
        (void)userdata;
        return false;
    }

    void getWindowSize(float &width, float &height)
    {
        int w = 0, h = 0;
        getWindowSize(w, h);
        width  = static_cast<float>(w);
        height = static_cast<float>(h);
    }
    virtual void getWindowSize(int &width, int &height) = 0;
    /// Iconified / minimized. Present surfaces skip acquire while this is true.
    [[nodiscard]] virtual bool isMinimized() const { return false; }
    virtual bool minimize() { return false; }
    virtual bool restoreFromMinimize() { return false; }
    /// Order-out without destroying the session (last-tab drag pickup).
    [[nodiscard]] virtual bool isHidden() const { return false; }
    virtual bool hide() { return false; }
    virtual bool show() { return false; }
    virtual bool setWindowSize(int width, int height)
    {
        (void) width;
        (void) height;
        YA_CORE_ERROR("setWindowSize not implemented in INativeWindow");
        return false;
    }
    virtual bool getWindowPosition(int& x, int& y) const
    {
        x = 0;
        y = 0;
        (void)x;
        (void)y;
        return false;
    }
    virtual bool setWindowPosition(int x, int y)
    {
        (void)x;
        (void)y;
        return false;
    }
    /// Index in the current `Os::displayCount()` list, or -1 if unknown.
    [[nodiscard]] virtual int getDisplayIndex() const { return -1; }
    [[nodiscard]] virtual std::string getDisplayName() const { return {}; }
    [[nodiscard]] virtual bool isMaximized() const { return false; }
    virtual bool maximize() { return false; }
    virtual bool restoreFromMaximize() { return false; }
    [[nodiscard]] virtual NativeWindowSafeArea getSafeArea() const { return {}; }
    virtual bool getBordersSize(int& top, int& left, int& bottom, int& right) const
    {
        (void)top;
        (void)left;
        (void)bottom;
        (void)right;
        return false;
    }

#if USE_VULKAN
    virtual bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface) = 0;
    virtual void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface) = 0;
    virtual std::vector<const char *> onGetVkInstanceExtensions() = 0;
#endif
};

/// SDL-backed concrete native window plus Vulkan surface hooks. Process-wide
/// OS APIs (events, cursor, clipboard, sleep) live in Core/Os, not here.
class YA_RHI_API SDLNativeWindow final : public INativeWindow
{
    NativeWindowHitTestFn _hitTest         = nullptr;
    void*                 _hitTestUserdata = nullptr;

  public:
    SDLNativeWindow() = default;
    ~SDLNativeWindow() override;

    bool init() override;
    void destroy() override;
    bool recreate(const WindowCreateInfo &ci) override;
    void setTitle(const std::string &title) override;
    bool setIcon(const std::string& path) override;
    [[nodiscard]] uint32_t getWindowID() const override;

    void getWindowSize(int &width, int &height) override;
    [[nodiscard]] bool isMinimized() const override;
    bool minimize() override;
    bool restoreFromMinimize() override;
    [[nodiscard]] bool isHidden() const override;
    bool hide() override;
    bool show() override;
    bool setWindowSize(int width, int height) override;
    bool getWindowPosition(int& x, int& y) const override;
    bool setWindowPosition(int x, int y) override;
    [[nodiscard]] int getDisplayIndex() const override;
    [[nodiscard]] std::string getDisplayName() const override;
    [[nodiscard]] bool isMaximized() const override;
    bool maximize() override;
    bool restoreFromMaximize() override;
    [[nodiscard]] NativeWindowSafeArea getSafeArea() const override;
    bool getBordersSize(int& top, int& left, int& bottom, int& right) const override;

    bool startTextInput() override;
    bool stopTextInput() override;
    bool setMouseGrab(bool grab) override;
    bool setRelativeMouseMode(bool relative) override;
    bool setMouseConfineRect(const Rect2D* rect) override;
    bool setGlobalMouseCapture(bool capture) override;
    bool setBordered(bool bordered) override;
    bool setMousePassthrough(bool enable) override;
    bool setHitTest(NativeWindowHitTestFn fn, void* userdata) override;
    [[nodiscard]] ENativeWindowHitResult invokeHitTest(float x, float y) const;

    /// Re-read the window's display content scale. Called at create time and
    /// whenever the window moves to a different monitor (Qt's per-monitor DPI
    /// trap: a stale scale makes text blurry or tiny after dragging screens).
    void refreshDpiScale() override;

#if USE_VULKAN
    bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    std::vector<const char *> onGetVkInstanceExtensions() override;
#endif
};
} // namespace ya
