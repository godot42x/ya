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

    struct DiffInfo
    {
        VkExtent2D       extent;
        VkPresentModeKHR presentMode;
    };
    MulticastDelegate<void(const DiffInfo &old, const DiffInfo &now)> onRecreate;

  public:
    VulkanSwapChain(VulkanRender *render)
        : _render(render)
    {
        YA_CORE_ASSERT(_render != nullptr, "VulkanRender is null!");
    }
    ~VulkanSwapChain();


    void cleanup();
    bool recreate(const SwapchainCreateInfo &ci);

    // Swap chain operations
    VkResult acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t &outImageIndex);
    VkResult presentImage(uint32_t imageIndex, std::vector<VkSemaphore> semaphores);

    void setVsync(bool vsync)
    {
        bVsync         = vsync;
        auto ci        = _ci;
        ci.bVsync      = bVsync;
        ci.presentMode = bVsync ? EPresentMode::FIFO : EPresentMode::Immediate;
        recreate(ci);
    }

    [[nodiscard]] const SwapchainCreateInfo &getCreateInfo() const { return _ci; }

    // Getters
    [[nodiscard]] VkSwapchainKHR              getSwapChain() const { return m_swapChain; }
    [[nodiscard]] const std::vector<VkImage> &getImages() const { return m_images; }
    [[nodiscard]] uint32_t                    getImageSize() const { return static_cast<uint32_t>(m_images.size()); }
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
