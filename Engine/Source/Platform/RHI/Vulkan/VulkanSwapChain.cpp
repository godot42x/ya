#include "VulkanSwapChain.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <algorithm>
#include <limits>


VkSurfaceFormatKHR VulkanSwapChainSupportDetails::ChooseSwapSurfaceFormat()
{
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return {
            .format     = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    for (const auto &available_format : formats)
    {
        if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_format;
        }
    }

    return formats[0];
}

VkPresentModeKHR VulkanSwapChainSupportDetails::ChooseSwapPresentMode()
{
    for (const auto &available_present_mode : present_modes)
    {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_mode;
        }
        else if (available_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return available_present_mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapChainSupportDetails::ChooseSwapExtent(WindowProvider *provider)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    provider->getWindowSize(width, height);
    VkExtent2D actualExtent = {(uint32_t)width, (uint32_t)height};

    actualExtent.width = std::max(
        capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(
        capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

VulkanSwapChainSupportDetails VulkanSwapChainSupportDetails::query(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VulkanSwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count != 0)
    {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count != 0)
    {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}

void VulkanSwapChain::initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice,
                                 VkSurfaceKHR surface, WindowProvider *windowProvider)
{
    m_logicalDevice  = logicalDevice;
    m_physicalDevice = physicalDevice;
    m_surface        = surface;
    m_windowProvider = windowProvider;
}

void VulkanSwapChain::create()
{
    VulkanSwapChainSupportDetails supportDetails = VulkanSwapChainSupportDetails::query(m_physicalDevice, m_surface);

    VkSurfaceFormatKHR surfaceFormat = supportDetails.ChooseSwapSurfaceFormat();
    VkPresentModeKHR   presentMode   = supportDetails.ChooseSwapPresentMode();

    m_extent      = supportDetails.ChooseSwapExtent(m_windowProvider);
    m_imageFormat = surfaceFormat.format;
    m_colorSpace  = surfaceFormat.colorSpace;

    uint32_t minImageCount = supportDetails.capabilities.minImageCount + 1;
    if (supportDetails.capabilities.maxImageCount > 0 &&
        minImageCount > supportDetails.capabilities.maxImageCount)
    {
        minImageCount = supportDetails.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = m_surface,
        .minImageCount    = minImageCount,
        .imageFormat      = m_imageFormat,
        .imageColorSpace  = m_colorSpace,
        .imageExtent      = m_extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = supportDetails.capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };

    // Set sharing mode based on queue families
    // This would need queue family indices - simplified for now
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateSwapchainKHR(m_logicalDevice, &ci, nullptr, &m_swapChain);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create swap chain!");

    // Get swap chain images
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(m_logicalDevice, m_swapChain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_logicalDevice, m_swapChain, &imageCount, m_images.data());

    createImageViews();
}

void VulkanSwapChain::cleanup()
{
    for (auto imageView : m_imageViews) {
        vkDestroyImageView(m_logicalDevice, imageView, nullptr);
    }
    m_imageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_logicalDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

void VulkanSwapChain::recreate()
{
    cleanup();
    create();
}

void VulkanSwapChain::createImageViews()
{
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); ++i)
    {
        m_imageViews[i] = VulkanUtils::createImageView(m_logicalDevice, m_images[i], m_imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

VkResult VulkanSwapChain::acquireNextImage(uint32_t &imageIndex, VkSemaphore semaphore)
{
    return vkAcquireNextImageKHR(m_logicalDevice, m_swapChain, std::numeric_limits<uint64_t>::max(), semaphore, VK_NULL_HANDLE, &imageIndex);
}

VkResult VulkanSwapChain::presentImage(uint32_t imageIndex, VkSemaphore waitSemaphore, VkQueue presentQueue)
{
    VkPresentInfoKHR presentInfo{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &waitSemaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &m_swapChain,
        .pImageIndices      = &imageIndex,
        .pResults           = nullptr,
    };

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}
