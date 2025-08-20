#include "VulkanSwapChain.h"
#include "Core/Log.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"
#include <algorithm>
#include <limits>



VkSurfaceFormatKHR VulkanSwapChainSupportDetails::ChooseSwapSurfaceFormat(VkSurfaceFormatKHR preferredSurfaceFormat)
{

    for (const auto &format : formats)
    {
        if (format.format == preferredSurfaceFormat.format && format.colorSpace == preferredSurfaceFormat.colorSpace)
        {
            return format;
        }
    }

    NE_CORE_WARN("Preferred surface format {} and color space {} not found, using first format",
                 std::to_string(preferredSurfaceFormat.format),
                 std::to_string(preferredSurfaceFormat.colorSpace));

    return formats[0];
}

VkPresentModeKHR VulkanSwapChainSupportDetails::ChooseSwapPresentMode(VkPresentModeKHR preferredMode)
{
    // First try to find the preferred mode
    for (const auto &modes : presentModes)
    {
        if (modes == preferredMode) {
            return modes;
        }
    }

    // If preferred mode not found, try fallback strategies
    NE_CORE_WARN("Preferred present mode {} not available", std::to_string(preferredMode));

    // If we wanted FIFO (VSync) but it's not available, warn user
    if (preferredMode == VK_PRESENT_MODE_FIFO_KHR) {
        NE_CORE_WARN("VK_PRESENT_MODE_FIFO_KHR not supported! VSync may not work correctly.");

        // Try FIFO_RELAXED as fallback for VSync
        for (const auto &modes : presentModes) {
            if (modes == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
                NE_CORE_INFO("Using VK_PRESENT_MODE_FIFO_RELAXED_KHR as fallback");
                return modes;
            }
        }
    }

    // Log available modes for debugging
    NE_CORE_INFO("Available present modes:");
    for (const auto &modes : presentModes) {
        NE_CORE_INFO("  - {}", std::to_string(modes));
    }

    // Use first available mode as last resort
    NE_CORE_WARN("Using first available present mode: {}", std::to_string(presentModes[0]));
    return presentModes[0];
}

VkExtent2D VulkanSwapChainSupportDetails::ChooseSwapExtent(IWindowProvider *provider, int preferredWidth, int preferredHeight)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent;
    if (preferredHeight == 0 && preferredWidth == 0) {
        int width, height;
        provider->getWindowSize(width, height);
        actualExtent.width  = static_cast<uint32_t>(width);
        actualExtent.height = static_cast<uint32_t>(height);
    }
    else {
        actualExtent.width  = static_cast<uint32_t>(preferredWidth);
        actualExtent.height = static_cast<uint32_t>(preferredHeight);
    }

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

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}



void VulkanSwapChain::cleanup()
{

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(_render->getLogicalDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

void VulkanSwapChain::recreate(const SwapchainCreateInfo &ci)
{
    cleanup();
    create(ci);
}



VulkanSwapChain::~VulkanSwapChain()
{
    cleanup();
}

bool VulkanSwapChain::create(const SwapchainCreateInfo &ci)
{
    _supportDetails = VulkanSwapChainSupportDetails::query(_render->getPhysicalDevice(), _render->getSurface());

    // Use config parameters instead of defaults
    VkSurfaceFormatKHR preferred;

    // Convert from abstract format to Vulkan format
    switch (_ci.imageFormat) {
    case EFormat::B8G8R8A8_UNORM:
        preferred.format = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    case EFormat::R8G8B8A8_UNORM:
        preferred.format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        UNIMPLEMENTED();
        break;
    }


    // Set color space based on config
    switch (_ci.colorSpace) {
    case EColorSpace::SRGB_NONLINEAR:
        preferred.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
    preferred = _supportDetails.ChooseSwapSurfaceFormat(preferred);



    // TODO: extend
    // Choose present mode based on config and V-Sync setting
    VkPresentModeKHR presentMode;
    if (_ci.bVsync) {
        presentMode = VK_PRESENT_MODE_FIFO_KHR; // V-Sync enabled
    }
    else {
        switch (_ci.presentMode) {
        case EPresentMode::Immediate:
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        case EPresentMode::Mailbox:
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case EPresentMode::FIFO:
            presentMode = VK_PRESENT_MODE_FIFO_KHR;
            break;
        default:
            UNIMPLEMENTED();
            break;
        }
    }

    _presentMode = _supportDetails.ChooseSwapPresentMode(presentMode);

    NE_CORE_INFO("Using chosen surface format: {} with color space: {}",
                 std::to_string(preferred.format),
                 std::to_string(preferred.colorSpace));
    NE_CORE_INFO("Requested present mode: {}, Actually using: {}",
                 std::to_string(presentMode),
                 std::to_string(_presentMode));
    NE_CORE_INFO("Current extent is: {}x{}",
                 _supportDetails.capabilities.currentExtent.width,
                 _supportDetails.capabilities.currentExtent.height);

    // Use configured dimensions or fall back to window size
    // if (_ci.width > 0 && _ci.height > 0) {
    //     m_extent.width  = _ci.width;
    //     m_extent.height = _ci.height;

    //     // Clamp to surface capabilities
    //     m_extent.width = std::max(
    //         _supportDetails.capabilities.minImageExtent.width,
    //         std::min(_supportDetails.capabilities.maxImageExtent.width, m_extent.width));
    //     m_extent.height = std::max(
    //         _supportDetails.capabilities.minImageExtent.height,
    //         std::min(_supportDetails.capabilities.maxImageExtent.height, m_extent.height));
    // }
    // else {
    //     m_extent = _supportDetails.ChooseSwapExtent(_render->_windowProvider);
    // }

    _surfaceFormat     = preferred.format;
    _surfaceColorSpace = preferred.colorSpace;

    _minImageCount = std::max(_ci.minImageCount, _supportDetails.capabilities.minImageCount);
    if (_supportDetails.capabilities.maxImageCount > 0 && _minImageCount > _supportDetails.capabilities.maxImageCount)
    {
        _minImageCount = _supportDetails.capabilities.maxImageCount;
    }

    VkSharingMode sharingMode           = {};
    uint32_t      queueFamilyCount      = 0;
    uint32_t      queueFamilyIndices[2] = {0, 0};
    if (_render->isGraphicsPresentSameQueueFamily())
    {
        sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    else {
        sharingMode           = VK_SHARING_MODE_CONCURRENT;
        queueFamilyIndices[0] = _render->getGraphicsQueueFamilyInfo().queueFamilyIndex;
        queueFamilyIndices[1] = _render->getPresentQueueFamilyInfo().queueFamilyIndex;
        queueFamilyCount      = 2;
    }

    VkSwapchainKHR oldSwapchain = m_swapChain;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }


    VkSwapchainCreateInfoKHR vkSwapchainCI{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface               = _render->getSurface(),
        .minImageCount         = _minImageCount,
        .imageFormat           = _surfaceFormat,
        .imageColorSpace       = _surfaceColorSpace,
        .imageExtent           = _supportDetails.capabilities.currentExtent, // reuse current extent
        .imageArrayLayers      = _ci.imageArrayLayers,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // Convert from config if needed
        .imageSharingMode      = sharingMode,                         // Exclusive mode for now
        .queueFamilyIndexCount = queueFamilyCount,
        .pQueueFamilyIndices   = queueFamilyIndices,
        .preTransform          = _supportDetails.capabilities.currentTransform,
        .compositeAlpha        = compositeAlpha,
        .presentMode           = _presentMode,
        .clipped               = _ci.bClipped ? VK_TRUE : VK_FALSE,
        .oldSwapchain          = oldSwapchain,
    };



    VkResult result = vkCreateSwapchainKHR(_render->getLogicalDevice(), &vkSwapchainCI, nullptr, &m_swapChain);
    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Swap chain creation failed {}", result);
        return false;
    }
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create swap chain!");

    NE_CORE_INFO("-- Create  swapchain success {}\n\told swapchain {}\n\tmin image count {}\n\tformat {}\n\tcolor space {}\n\tpresent mode {}\n\tsharing mode {}",
                 (uintptr_t)m_swapChain,
                 (uintptr_t)oldSwapchain,
                 _minImageCount,
                 std::to_string(_surfaceFormat),
                 std::to_string(_surfaceColorSpace),
                 std::to_string(_presentMode),
                 std::to_string(sharingMode));



    // Get swap chain images
    uint32_t imageCount = -1;
    vkGetSwapchainImagesKHR(_render->getLogicalDevice(), m_swapChain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(_render->getLogicalDevice(), m_swapChain, &imageCount, m_images.data());
    NE_CORE_INFO("Swapchain finally created with {} images", m_images.size());

    return true;
}

VkResult VulkanSwapChain::acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t &outImageIndex)
{
    VkResult result = vkAcquireNextImageKHR(_render->getLogicalDevice(),
                                            m_swapChain,
                                            UINT64_MAX,
                                            semaphore,
                                            fence,
                                            &outImageIndex);
    _curImageIndex  = outImageIndex;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        NE_CORE_WARN("Swap chain is out of date or suboptimal: {}", result);
        return result;
    }

    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to acquire next image: {}", result);
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        VK_CALL(vkWaitForFences(_render->getLogicalDevice(),
                                1,
                                &fence,
                                VK_TRUE,
                                UINT64_MAX));
        VK_CALL(vkResetFences(_render->getLogicalDevice(),
                              1,
                              &fence));
    }

    return result;
}

VkResult VulkanSwapChain::presentImage(uint32_t imageIndex, VkQueue presentQueue, std::vector<VkSemaphore> waitSemaphores)
{
    VkPresentInfoKHR presentInfo{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = nullptr,
        .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
        .pWaitSemaphores    = waitSemaphores.data(),
        .swapchainCount     = 1, // ???
        .pSwapchains        = &m_swapChain,
        .pImageIndices      = &imageIndex,
    };

    VkResult ret = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (ret != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to present swap chain image: {}", ret);
    }
    return ret;
}
