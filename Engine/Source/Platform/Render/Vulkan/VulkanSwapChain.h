#pragma once

#include "Render/Render.h"
#include "WindowProvider.h"
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanSwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat();
    VkPresentModeKHR   ChooseSwapPresentMode();
    VkExtent2D         ChooseSwapExtent(WindowProvider *provider);
    bool               isValidFormat(const VkSurfaceFormatKHR &fmt);

    static VulkanSwapChainSupportDetails query(VkPhysicalDevice device, VkSurfaceKHR surface);
};

class VulkanSwapChain
{
  private:
    VkDevice         m_logicalDevice  = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    WindowProvider  *m_windowProvider = nullptr;

    VkSwapchainKHR           m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat                 m_imageFormat;
    VkColorSpaceKHR          m_colorSpace;
    VkExtent2D               m_extent;

    SwapchainCreateInfo      _ci;

  public:
    VulkanSwapChain()  = default;
    ~VulkanSwapChain() = default;

    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice,
                    VkSurfaceKHR surface, WindowProvider *windowProvider, const SwapchainCreateInfo &ci);

    void create();
    void cleanup();
    void recreate();

    // Getters
    VkSwapchainKHR                  getSwapChain() const { return m_swapChain; }
    const std::vector<VkImage>     &getImages() const { return m_images; }
    const std::vector<VkImageView> &getImageViews() const { return m_imageViews; }
    VkFormat                        getImageFormat() const { return m_imageFormat; }
    VkExtent2D                      getExtent() const { return m_extent; }

    // Swap chain operations
    VkResult acquireNextImage(uint32_t &imageIndex, VkSemaphore semaphore);
    VkResult presentImage(uint32_t imageIndex, VkSemaphore semaphore, VkQueue presentQueue);

  private:
    void createImageViews();
};
