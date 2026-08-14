
#pragma once

#include "Core/Log.h"
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

struct IWindowProvider
{
  protected:
    void *nativeWindowHandle = nullptr;
    float dpiScale           = 1.0f; // DPI scale factor, default is 1.0

  public:
    virtual ~IWindowProvider()
    {
        YA_CORE_TRACE("IWindowProvider::~IWindowProvider()");
    }

    [[nodiscard]] void *getNativeWindowHandle() const { return nativeWindowHandle; }

    // TODO: support multiple windows
    virtual bool init()                               = 0;
    virtual void destroy()                            = 0;
    virtual bool recreate(const WindowCreateInfo &ci) = 0;
    virtual void setTitle(const std::string &title)    = 0;
    [[nodiscard]] virtual uint32_t getWindowID() const = 0;

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

#if USE_VULKAN
    virtual bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface) = 0;
    virtual void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface) = 0;
    virtual std::vector<const char *> onGetVkInstanceExtensions() = 0;
#endif
};

class YA_RHI_API SDLWindowProvider final : public IWindowProvider
{
  public:
    SDLWindowProvider() = default;
    ~SDLWindowProvider() override;

    bool init() override;
    void destroy() override;
    bool recreate(const WindowCreateInfo &ci) override;
    void setTitle(const std::string &title) override;
    [[nodiscard]] uint32_t getWindowID() const override;

    void getWindowSize(int &width, int &height) override;
    bool setWindowSize(int width, int height) override;

#if USE_VULKAN
    bool onCreateVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    void onDestroyVkSurface(VkInstance instance, VkSurfaceKHR *surface) override;
    std::vector<const char *> onGetVkInstanceExtensions() override;
#endif
};
} // namespace ya
