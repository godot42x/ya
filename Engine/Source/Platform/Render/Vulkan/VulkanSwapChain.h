#pragma once

#include "Core/Delegate.h"
#include "Render/Render.h"
#include "WindowProvider.h"
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanSwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(VkSurfaceFormatKHR preferredSurfaceFormat);
    VkPresentModeKHR   ChooseSwapPresentMode(VkPresentModeKHR preferredMode);
    VkExtent2D         ChooseSwapExtent(IWindowProvider *provider, int preferredWidth = 0, int preferredHeight = 0);

    static VulkanSwapChainSupportDetails query(VkPhysicalDevice device, VkSurfaceKHR surface);
};

struct VulkanRender;

struct VulkanSwapChain
{
    VulkanRender *_render = nullptr;

    VkSwapchainKHR                m_swapChain = VK_NULL_HANDLE;
    VulkanSwapChainSupportDetails _supportDetails;

    std::vector<VkImage> m_images;

    VkFormat         _surfaceFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR  _surfaceColorSpace;
    uint32_t         _minImageCount = 0; // Minimum image count based on surface capabilities
    VkPresentModeKHR _presentMode;
    bool             bVsync = true;

    SwapchainCreateInfo _ci;

    uint32_t _curImageIndex = 0;

    MulticastDelegate<void()> onRecreate;

  public:
    VulkanSwapChain(VulkanRender *render)
        : _render(render)
    {
        NE_CORE_ASSERT(_render != nullptr, "VulkanRender is null!");
    }
    ~VulkanSwapChain();


    void cleanup();
    bool recreate(const SwapchainCreateInfo &ci);

    // Swap chain operations
    VkResult acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t &outImageIndex);
    VkResult presentImage(uint32_t imageIndex, std::vector<VkSemaphore> semaphores);

    void setVsync(bool vsync)
    {
        bVsync          = vsync;
        _presentMode    = bVsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
        _ci.bVsync      = bVsync;
        _ci.presentMode = bVsync ? EPresentMode::FIFO : EPresentMode::Immediate;
        recreate(_ci);
    }

    [[nodiscard]] const SwapchainCreateInfo &getCreateInfo() const { return _ci; }

    // Getters
    [[nodiscard]] VkSwapchainKHR              getSwapChain() const { return m_swapChain; }
    [[nodiscard]] const std::vector<VkImage> &getImages() const { return m_images; }
    [[nodiscard]] VkFormat                    getSurfaceFormat() const { return _surfaceFormat; }
    [[nodiscard]] VkPresentModeKHR            getPresentMode() const { return _presentMode; }
    [[nodiscard]] uint32_t                    getWidth() const { return _supportDetails.capabilities.currentExtent.width; }
    [[nodiscard]] uint32_t                    getHeight() const { return _supportDetails.capabilities.currentExtent.height; };


    [[nodiscard]] VkExtent2D getExtent() const
    {
        return {
            _supportDetails.capabilities.currentExtent.width,
            _supportDetails.capabilities.currentExtent.height,
        };
    }

    [[nodiscard]] uint32_t getCurImageIndex() const { return _curImageIndex; }



  private:
};
