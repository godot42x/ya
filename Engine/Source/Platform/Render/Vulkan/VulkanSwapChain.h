#pragma once

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
    VkExtent2D         ChooseSwapExtent(WindowProvider *provider, int preferredWidth = 0, int preferredHeight = 0);

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

    SwapchainCreateInfo _ci;

  public:
    VulkanSwapChain(VulkanRender *render)
        : _render(render)
    {
        NE_CORE_ASSERT(_render != nullptr, "VulkanRender is null!");
    }
    ~VulkanSwapChain();


    bool create(const SwapchainCreateInfo &ci);
    void cleanup();
    void recreate(const SwapchainCreateInfo &ci);

    [[nodiscard]] const SwapchainCreateInfo &getCreateInfo() const { return _ci; }

    // Getters
    [[nodiscard]] VkSwapchainKHR              getSwapChain() const { return m_swapChain; }
    [[nodiscard]] const std::vector<VkImage> &getImages() const { return m_images; }
    [[nodiscard]] VkFormat                    getSurfaceFormat() const { return _surfaceFormat; }
    [[nodiscard]] uint32_t                    getWidth() const { return _supportDetails.capabilities.currentExtent.width; }
    [[nodiscard]] uint32_t                    getHeight() const { return _supportDetails.capabilities.currentExtent.height; };

    // Swap chain operations
    VkResult acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t &outImageIndex);
    VkResult presentImage(uint32_t imageIndex, VkQueue presentQueue, std::vector<VkSemaphore> semaphores);

    VkExtent2D getExtent() const
    {
        return {
            _supportDetails.capabilities.currentExtent.width,
            _supportDetails.capabilities.currentExtent.height,
        };
    }


  private:
};
