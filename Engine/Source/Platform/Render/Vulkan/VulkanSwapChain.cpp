#include "VulkanSwapChain.h"

#include "Core/Log.h"

#include "VulkanRender.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <limits>

namespace ya
{



VkSurfaceFormatKHR VulkanSwapChainSupportDetails::ChooseSwapSurfaceFormat(::VkSurfaceFormatKHR preferredSurfaceFormat)
{

    for (const auto &format : formats)
    {
        if (format.format == preferredSurfaceFormat.format && format.colorSpace == preferredSurfaceFormat.colorSpace)
        {
            return format;
        }
    }

    YA_CORE_WARN("Preferred surface format {} and color space {} not found, using first format",
                 std::to_string(preferredSurfaceFormat.format),
                 std::to_string(preferredSurfaceFormat.colorSpace));

    return formats[0];
}

VkPresentModeKHR VulkanSwapChainSupportDetails::ChooseSwapPresentMode(::VkPresentModeKHR preferredMode)
{
    // First try to find the preferred mode
    for (const auto &modes : presentModes)
    {
        if (modes == preferredMode) {
            return modes;
        }
    }

    // If preferred mode not found, try fallback strategies
    YA_CORE_WARN("Preferred present mode {} not available", std::to_string(preferredMode));

    // If we wanted FIFO (VSync) but it's not available, warn user
    if (preferredMode == VK_PRESENT_MODE_FIFO_KHR) {
        YA_CORE_WARN("VK_PRESENT_MODE_FIFO_KHR not supported! VSync may not work correctly.");

        // Try FIFO_RELAXED as fallback for VSync
        for (const auto &modes : presentModes) {
            if (modes == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
                YA_CORE_INFO("Using VK_PRESENT_MODE_FIFO_RELAXED_KHR as fallback");
                return modes;
            }
        }
    }

    // Log available modes for debugging
    YA_CORE_INFO("Available present modes:");
    for (const auto &modes : presentModes) {
        YA_CORE_INFO("  - {}", std::to_string(modes));
    }

    // Use first available mode as last resort
    YA_CORE_WARN("Using first available present mode: {}", std::to_string(presentModes[0]));
    return presentModes[0];
}

VkExtent2D VulkanSwapChainSupportDetails::ChooseSwapExtent(IWindowProvider *provider, int preferredWidth, int preferredHeight)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent;
    if (preferredHeight == 0 && preferredWidth == 0) {
        int width = 0, height = 0;
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

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
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
        vkDestroySwapchainKHR(_render->getDevice(), m_swapChain, _render->getAllocator());
        m_swapChain = VK_NULL_HANDLE;
    }
}

VulkanSwapChain::~VulkanSwapChain()
{
    cleanup();
}

// Helper: Validate if extent is valid (not minimized)
bool VulkanSwapChain::validateExtent(const VkExtent2D &extent) const
{
    if (extent.width == 0 || extent.height == 0) {
        YA_CORE_WARN("Window is minimized (extent 0x0), skipping swapchain recreation");
        return false;
    }
    if (extent.width <= 0 || extent.height <= 0) {
        YA_CORE_ERROR("Current extent is invalid!");
        return false;
    }
    return true;
}

// Helper: Select surface format based on config
void VulkanSwapChain::selectSurfaceFormat(const SwapchainCreateInfo &ci)
{
    VkSurfaceFormatKHR preferred{
        .format     = toVk(ci.imageFormat),
        .colorSpace = toVk(ci.colorSpace),
    };

    preferred = _supportDetails.ChooseSwapSurfaceFormat(preferred);

    _surfaceFormat     = preferred.format;
    _surfaceColorSpace = preferred.colorSpace;

    YA_CORE_INFO("Using chosen surface format: {} with color space: {}",
                 std::to_string(_surfaceFormat),
                 std::to_string(_surfaceColorSpace));
}

// Helper: Select present mode
void VulkanSwapChain::selectPresentMode(const SwapchainCreateInfo &ci)
{
    _presentMode = toVk(ci.presentMode);
    // TODO: Add compatibility fallback if needed:
    // _presentMode = _supportDetails.ChooseSwapPresentMode(_presentMode);
}

// Helper: Calculate image count based on capabilities
void VulkanSwapChain::calculateImageCount(const SwapchainCreateInfo &ci)
{
    _minImageCount = std::max(ci.minImageCount, _supportDetails.capabilities.minImageCount);
    if (_supportDetails.capabilities.maxImageCount > 0 && _minImageCount > _supportDetails.capabilities.maxImageCount)
    {
        _minImageCount = _supportDetails.capabilities.maxImageCount;
    }
}

// Helper: Select composite alpha mode
VkCompositeAlphaFlagBitsKHR VulkanSwapChain::selectCompositeAlpha() const
{
    if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    else if (_supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

// Helper: Setup queue family sharing mode
void VulkanSwapChain::setupQueueFamilySharing(VkSharingMode &sharingMode, uint32_t &queueFamilyCount, uint32_t queueFamilyIndices[2]) const
{
    if (_render->isGraphicsPresentSameQueueFamily())
    {
        sharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        queueFamilyCount = 0;
    }
    else {
        sharingMode           = VK_SHARING_MODE_CONCURRENT;
        queueFamilyIndices[0] = _render->getGraphicsQueueFamilyInfo().queueFamilyIndex;
        queueFamilyIndices[1] = _render->getPresentQueueFamilyInfo().queueFamilyIndex;
        queueFamilyCount      = 2;
    }
}


// Helper: Create swapchain and retrieve images
bool VulkanSwapChain::createSwapchainAndImages(const VkSwapchainCreateInfoKHR &vkCI)
{
    VkResult result = vkCreateSwapchainKHR(_render->getDevice(), &vkCI, nullptr, &m_swapChain);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Swap chain creation failed {}", result);
        return false;
    }

    // Get swap chain images
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(_render->getDevice(), m_swapChain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(_render->getDevice(), m_swapChain, &imageCount, m_images.data());

    YA_CORE_INFO("Created swapchain success:{} with [{}] images of format [{}] and color space [{}], present mode [{}], extent {}x{}",
                 (uintptr_t)m_swapChain,
                 imageCount,
                 std::to_string(_surfaceFormat),
                 std::to_string(_surfaceColorSpace),
                 std::to_string(_presentMode),
                 vkCI.imageExtent.width,
                 vkCI.imageExtent.height);

    return true;
}



// Helper: Broadcast recreate event
void VulkanSwapChain::handleCIChanged(SwapchainCreateInfo const &ci)
{

    YA_PROFILE_SCOPE("SwapChain recreate event");
    DiffInfo old{
        .extent = Extent2D{
            .width  = ci.width,
            .height = ci.height,
        },
        .presentMode = ci.presentMode,
    };

    // update
    _ci             = ci;
    _ci.width       = _supportDetails.capabilities.currentExtent.width;
    _ci.height      = _supportDetails.capabilities.currentExtent.height;
    _ci.presentMode = EPresentMode::fromVk(_presentMode);

    DiffInfo now{
        .extent = Extent2D{
            .width  = _ci.width,
            .height = _ci.height,
        },
        .presentMode = EPresentMode::fromVk(_presentMode),
    };
    onRecreate.broadcast(old, now);
}

bool VulkanSwapChain::recreate(const SwapchainCreateInfo &ci)
{
    YA_PROFILE_SCOPE("Swapchain Recreate");
    static uint32_t version = 0;
    version++;

    // Query surface capabilities
    _supportDetails       = VulkanSwapChainSupportDetails::query(_render->getPhysicalDevice(), _render->getSurface());
    const auto &newExtent = _supportDetails.capabilities.currentExtent;

    // Validate extent (check for minimized window)
    if (!validateExtent(newExtent)) {
        return true; // Will retry when window is restored
    }

    // Wait for GPU to finish before destroying old swapchain
    VkDevice device     = _render->getDevice();
    VkResult waitResult = vkDeviceWaitIdle(device);
    if (waitResult != VK_SUCCESS) {
        YA_CORE_ERROR("Failed to wait for device idle before swapchain recreation: {}", (int)waitResult);
        return false;
    }

    VkSwapchainKHR oldSwapchain = m_swapChain;
    cleanup();

    // Select swapchain parameters
    selectSurfaceFormat(ci);
    selectPresentMode(ci);
    calculateImageCount(ci);

    // Setup queue family sharing
    VkSharingMode sharingMode           = {};
    uint32_t      queueFamilyCount      = 0;
    uint32_t      queueFamilyIndices[2] = {0, 0};
    setupQueueFamilySharing(sharingMode, queueFamilyCount, queueFamilyIndices);

    // Build create info
    VkSwapchainCreateInfoKHR vkSwapchainCI{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext                 = nullptr,
        .flags                 = 0,
        .surface               = _render->getSurface(),
        .minImageCount         = _minImageCount,
        .imageFormat           = _surfaceFormat,
        .imageColorSpace       = _surfaceColorSpace,
        .imageExtent           = _supportDetails.capabilities.currentExtent,
        .imageArrayLayers      = ci.imageArrayLayers,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode      = sharingMode,
        .queueFamilyIndexCount = queueFamilyCount,
        .pQueueFamilyIndices   = queueFamilyIndices,
        .preTransform          = _supportDetails.capabilities.currentTransform,
        .compositeAlpha        = selectCompositeAlpha(),
        .presentMode           = _presentMode,
        .clipped               = ci.bClipped ? VK_TRUE : VK_FALSE,
        .oldSwapchain          = oldSwapchain,
    };

    // Create swapchain and retrieve images
    if (!createSwapchainAndImages(vkSwapchainCI)) {
        return false;
    }

    // Set debug name
    _render->setDebugObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR,
                                m_swapChain,
                                std::format("SwapChain_{}", version).c_str());

    handleCIChanged(ci);

    return true;
}

VkResult VulkanSwapChain::acquireNextImage(VkSemaphore semaphore, VkFence fence, uint32_t &outImageIdx)
{
    auto     device = _render->getDevice();
    VkResult result = vkAcquireNextImageKHR(device,
                                            m_swapChain,
                                            UINT64_MAX,
                                            semaphore,
                                            fence,
                                            &outImageIdx);

    if (result == VK_SUCCESS) {
        if (fence != VK_NULL_HANDLE) {
            VK_CALL(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
            VK_CALL(vkResetFences(device, 1, &fence));
        }
    }
    else
    {
        if (result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
            YA_CORE_ERROR("Failed to acquire next image: {}", result);
            return result;
        }
        else {
            YA_CORE_WARN("Swap chain is out of date or suboptimal: {}", result);
        }
    }
    _curImageIndex = outImageIdx;
    return result;
}

VkResult VulkanSwapChain::presentImage(uint32_t idx, std::vector<VkSemaphore> waitSemaphores)
{
    VkResult         result = {};
    VkPresentInfoKHR presentInfo{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = nullptr,
        .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
        .pWaitSemaphores    = waitSemaphores.data(),
        .swapchainCount     = 1, // ???
        .pSwapchains        = &m_swapChain,
        .pImageIndices      = &idx,
        .pResults           = &result,
    };

    VkResult ret = vkQueuePresentKHR(_render->getPresentQueues()[0].getHandle(), &presentInfo);
    if (ret != VK_SUCCESS) {
        if (ret == VK_SUBOPTIMAL_KHR || ret == VK_ERROR_OUT_OF_DATE_KHR) {
            YA_CORE_WARN("Swap chain is out of date or suboptimal {}: {}", idx, ret);
        }
        else {
            YA_CORE_ERROR("Failed to present swap chain image {}: {}", idx, ret);
        }
    }
    return ret;
}

// ISwapchain interface implementation
EFormat::T VulkanSwapChain::getFormat() const
{
    // Convert VkFormat to EFormat::T
    switch (_surfaceFormat) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return EFormat::R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return EFormat::B8G8R8A8_UNORM;
    default:
        return EFormat::Undefined;
    }
}

} // namespace ya
