
#pragma once

#include "Core/Log.h"
#include "Core/Common/Types.h"
#include "RHI/Render.h"

#include <string>
#include <vector>

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
    virtual bool setWindowSize(int width, int height)
    {
        (void) width;
        (void) height;
        YA_CORE_ERROR("setWindowSize not implemented in INativeWindow");
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
  public:
    SDLNativeWindow() = default;
    ~SDLNativeWindow() override;

    bool init() override;
    void destroy() override;
    bool recreate(const WindowCreateInfo &ci) override;
    void setTitle(const std::string &title) override;
    [[nodiscard]] uint32_t getWindowID() const override;

    void getWindowSize(int &width, int &height) override;
    [[nodiscard]] bool isMinimized() const override;
    bool minimize() override;
    bool restoreFromMinimize() override;
    bool setWindowSize(int width, int height) override;

    bool startTextInput() override;
    bool stopTextInput() override;
    bool setMouseGrab(bool grab) override;
    bool setRelativeMouseMode(bool relative) override;
    bool setMouseConfineRect(const Rect2D* rect) override;

    /// Re-read the window's display content scale. Called at create time and
    /// whenever the window moves to a different monitor (Qt's per-monitor DPI
    /// trap: a stale scale makes text blurry or tiny after dragging screens).
    void refreshDpiScale();

#if USE_VULKAN
    bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    std::vector<const char *> onGetVkInstanceExtensions() override;
#endif
};
} // namespace ya
