#include "Platform/Render/Vulkan/VulkanDevice.h"
#include "Render/Render.h"

struct VulkanRender : public IRender
{
    VulkanState     _vulkanState;
    WindowProvider *windowProvider = nullptr;

    // Store initialization parameters for later use
    SwapchainCreateInfo  m_swapchainCI;
    RenderPassCreateInfo m_renderPassCI;
    bool                 m_bVsync = true;

    virtual bool init(const InitParams &params) override
    {
        // Store parameters
        windowProvider = &params.windowProvider;
        m_swapchainCI  = params.swapchainCI;
        m_renderPassCI = params.renderPassCI;
        m_bVsync       = params.bVsync;

#if USE_SDL
        auto sdlWindow = static_cast<SDLWindowProvider *>(windowProvider);
        _vulkanState.onCreateSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            return sdlWindow->onCreateVkSurface(instance, surface);
        });
        _vulkanState.onReleaseSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            sdlWindow->onDestroyVkSurface(instance, surface);
        });
        _vulkanState.onGetRequiredExtensions.set([sdlWindow]() {
            return sdlWindow->onGetVkInstanceExtensions();
        });
#endif

        // Apply swapchain configuration to window if needed
        if (m_swapchainCI.width > 0 && m_swapchainCI.height > 0) {
            windowProvider->setWindowSize(m_swapchainCI.width, m_swapchainCI.height);
        }

        // Initialize Vulkan state with all configurations
        bool success = _vulkanState.init(windowProvider, m_swapchainCI, m_renderPassCI, m_bVsync);
        if (!success) {
            NE_CORE_ERROR("Failed to initialize Vulkan state");
            return false;
        }

        // Apply render pass and swapchain configurations
        applyInitConfiguration();

        return true;
    }

    void destroy() override
    {
        _vulkanState.destroy();
        windowProvider = nullptr;
    }

  private:
    void applyInitConfiguration()
    {
        // Apply V-Sync setting
        if (!m_bVsync) {
            // Configure for immediate mode presentation if V-Sync is disabled
            // This would require modifications to the swap chain creation
            NE_CORE_INFO("V-Sync disabled - using immediate presentation mode");
        }

        // Log configuration
        NE_CORE_INFO("VulkanRender initialized with:");
        NE_CORE_INFO("  - V-Sync: {}", m_bVsync ? "Enabled" : "Disabled");
        NE_CORE_INFO("  - Swapchain: {}x{}", m_swapchainCI.width, m_swapchainCI.height);
        NE_CORE_INFO("  - Present Mode: {}", static_cast<int>(m_swapchainCI.presentMode));
        NE_CORE_INFO("  - Image Format: {}", static_cast<int>(m_swapchainCI.imageFormat));
    }
};