#pragma once

#include "Core/Delegate.h"
#include "Platform/Render/Vulkan/VulkanUtils.h"
#include "Render/Core/Swapchain.h"
#include "WindowProvider.h"
#include <vector>

#include <vulkan/vulkan_core.h>


namespace ya
{



struct VulkanSwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(VkSurfaceFormatKHR preferredSurfaceFormat);
    VkPresentModeKHR   ChooseSwapPresentMode(const VkPresentModeKHR preferredMode) const;
    VkExtent2D         ChooseSwapExtent(IWindowProvider *provider, int preferredWidth = 0, int preferredHeight = 0);

    static VulkanSwapChainSupportDetails query(VkPhysicalDevice device, VkSurfaceKHR surface);
};

struct VulkanRender;

struct VulkanSwapChain : public ISwapchain
{
    VulkanRender *_render = nullptr;

    VkSwapchainKHR                m_swapChain = VK_NULL_HANDLE;
    VulkanSwapChainSupportDetails _supportDetails;

    std::vector<VkImage> _images;

    VkFormat         _surfaceFormat     = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR  _surfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint32_t         _minImageCount     = 0; // Minimum image count based on surface capabilities
    VkPresentModeKHR _presentMode       = VK_PRESENT_MODE_FIFO_KHR;
    bool             bVsync             = true;

    SwapchainCreateInfo _ci;

    uint32_t _curImageIndex = 0;


  public:
    VulkanSwapChain(VulkanRender *render)
        : _render(render)
    {
        YA_CORE_ASSERT(_render != nullptr, "VulkanRender is null!");
    }
    ~VulkanSwapChain();

    // Delete copy operations (this is a resource-owning class)
    VulkanSwapChain(const VulkanSwapChain &)            = delete;
    VulkanSwapChain &operator=(const VulkanSwapChain &) = delete;

    // Default move operations
    VulkanSwapChain(VulkanSwapChain &&)            = default;
    VulkanSwapChain &operator=(VulkanSwapChain &&) = default;

    void cleanup();

    // Swap chain operations
    VkResult acquireNextImage(::VkSemaphore semaphore, ::VkFence fence, uint32_t &outImageIndex);
    VkResult presentImage(uint32_t imageIndex, std::vector<::VkSemaphore> semaphores);

  private:
    // Helper functions for swapchain recreation
    bool                        validateExtent(const VkExtent2D &extent) const;
    void                        selectSurfaceFormat(const SwapchainCreateInfo &ci);
    void                        selectPresentMode(const SwapchainCreateInfo &ci);
    void                        calculateImageCount(const SwapchainCreateInfo &ci);
    VkCompositeAlphaFlagBitsKHR selectCompositeAlpha() const;

    void setupQueueFamilySharing(VkSharingMode &sharingMode, uint32_t &queueFamilyCount, uint32_t queueFamilyIndices[2]) const;
    bool createSwapchainAndImages(const VkSwapchainCreateInfoKHR &vkCI, bool &bImageRecreated);
    void updateMemberVariables(const SwapchainCreateInfo &ci);
    void handleCIChanged(SwapchainCreateInfo const &newCI, bool bImageRecreated);

  public:

    [[nodiscard]] const ya::SwapchainCreateInfo &getCreateInfo() const { return _ci; }

    // Getters (Vulkan-specific, kept for backward compatibility)
    [[nodiscard]] VkSwapchainKHR   getSwapchain() const { return m_swapChain; }
    [[nodiscard]] uint32_t         getImageSize() const { return static_cast<uint32_t>(_images.size()); }
    [[nodiscard]] VkFormat         getSurfaceFormat() const { return _surfaceFormat; }
    [[nodiscard]] VkPresentModeKHR getVkPresentMode() const { return _presentMode; }
    [[nodiscard]] uint32_t         getWidth() const { return _supportDetails.capabilities.currentExtent.width; }
    [[nodiscard]] uint32_t         getHeight() const { return _supportDetails.capabilities.currentExtent.height; }

    [[nodiscard]] uint32_t getCurImageIndex() const override { return _curImageIndex; }

    // ISwapchain interface implementation
    void *getHandle() const override { return static_cast<void *>(m_swapChain); }

    bool     recreate(const ya::SwapchainCreateInfo &ci) override;
    Extent2D getExtent() const override
    {
        return {
            .width  = _supportDetails.capabilities.currentExtent.width,
            .height = _supportDetails.capabilities.currentExtent.height};
    }

    // ISwapchain interface implementation
    EFormat::T getFormat() const override { return EFormat::fromVk(_surfaceFormat); }
    uint32_t   getImageCount() const override { return static_cast<uint32_t>(_images.size()); }

    // ISwapchain interface: VSync control
    bool getVsync() const override { return bVsync; }

    void setVsync(bool vsync) override
    {
        bVsync            = vsync;
        auto newCI        = _ci;
        newCI.bVsync      = bVsync;
        newCI.presentMode = bVsync ? EPresentMode::FIFO : EPresentMode::Immediate;
        recreate(newCI);
    }

    void setPresentMode(EPresentMode::T presentMode) override
    {
        auto ci        = _ci;
        ci.presentMode = presentMode;
        ci.bVsync      = (presentMode == EPresentMode::FIFO);
        recreate(ci);
    }

    EPresentMode::T getPresentMode() const override;


    bool acquireNextImage(void *semaphore, void *fence, uint32_t *imageIndex);
    bool present(uint32_t imageIndex);

    // Vulkan-specific getters (for backward compatibility)
    [[nodiscard]] VkSwapchainKHR              getVkHandle() const { return m_swapChain; }
    [[nodiscard]] const std::vector<VkImage> &getVkImages() const { return _images; }
    [[nodiscard]] VkExtent2D                  getVkExtent() const
    {
        return {
            _supportDetails.capabilities.currentExtent.width,
            _supportDetails.capabilities.currentExtent.height,
        };
    }

    [[nodiscard]] std::vector<EPresentMode::T> getAvailablePresentModes() const override;
};

}; // namespace ya