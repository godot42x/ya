#include "VulkanSwapChain.h"

#include "Core/Log.h"

#include "VulkanRender.h"
#include "VulkanUtils.h"
#include "utility.cc/ranges.h"

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

VkPresentModeKHR VulkanSwapChainSupportDetails::ChooseSwapPresentMode(const VkPresentModeKHR preferredMode) const
{
    // First try to find the preferred mode
    for (const auto &mode : presentModes)
    {
        if (mode == preferredMode) {
            return mode;
        }
    }
    // If preferred mode not found, try fallback strategies
    YA_CORE_ERROR("Preferred present mode {} not available", std::to_string(preferredMode));
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

    // Log available modes for debugging
    YA_CORE_INFO("Available present modes:");
    for (const auto &modes : details.presentModes) {
        YA_CORE_INFO("  - {}", std::to_string(modes));
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

    YA_CORE_TRACE("Using chosen surface format: {} with color space: {}",
                  std::to_string(_surfaceFormat),
                  std::to_string(_surfaceColorSpace));
}

// Helper: Select present mode
void VulkanSwapChain::selectPresentMode(const SwapchainCreateInfo &ci)
{
    _presentMode = toVk(ci.presentMode);
    // TODO: Add compatibility fallback if needed:
    _presentMode = _supportDetails.ChooseSwapPresentMode(_presentMode);
    bVsync       = (_presentMode == VK_PRESENT_MODE_FIFO_KHR);
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
void VulkanSwapChain::setupQueueFamilySharing(VkSharingMode &sharingMode,
                                              uint32_t      &queueFamilyCount,
                                              uint32_t       queueFamilyIndices[2]) const
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
bool VulkanSwapChain::createSwapchainAndImages(const VkSwapchainCreateInfoKHR &vkCI, bool &bImageRecreated)
{
    VkResult result = vkCreateSwapchainKHR(_render->getDevice(), &vkCI, nullptr, &m_swapChain);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Swap chain creation failed {}", result);
        return false;
    }

    // Get swap chain images
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(_render->getDevice(), m_swapChain, &imageCount, nullptr);
    _images.resize(imageCount);
    vkGetSwapchainImagesKHR(_render->getDevice(), m_swapChain, &imageCount, _images.data());
    bImageRecreated = true;

    YA_CORE_TRACE("Created swapchain success:{} with [{}] images of format [{}] and color space [{}], present mode [{}], extent {}x{}",
                  (uintptr_t)m_swapChain,
                  imageCount,
                  std::to_string(_surfaceFormat),
                  std::to_string(_surfaceColorSpace),
                  std::to_string(_presentMode),
                  vkCI.imageExtent.width,
                  vkCI.imageExtent.height);
    for (auto &&[i, img] : _images | ut::enumerate) {
        _render->setDebugObjectName(
            VK_OBJECT_TYPE_IMAGE,
            img,
            std::format("SwapChain_Image_{}", i).c_str());
    }

    return true;
}



// Helper: Broadcast recreate event
void VulkanSwapChain::handleCIChanged(SwapchainCreateInfo const &newCI, bool bImageRecreated)
{

    YA_PROFILE_SCOPE("SwapChain recreate event");
    DiffInfo old{
        .extent = Extent2D{
            .width  = _ci.width,
            .height = _ci.height,
        },
        .presentMode = newCI.presentMode,
    };

    // update
    _ci             = newCI;
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
    onRecreate.broadcast(old, now, bImageRecreated);
}

bool VulkanSwapChain::recreate(const SwapchainCreateInfo &newCI)
{
    YA_CORE_TRACE("======================================================");
    YA_PROFILE_SCOPE("Swapchain Recreate");
    static uint32_t version = 0;
    version++;

    // Query surface capabilities
    _supportDetails = VulkanSwapChainSupportDetails::query(
        _render->getPhysicalDevice(),
        _render->getSurface());
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
    // leave the old swapchain for ci
    // Validation Error: [ VUID-VkSwapchainCreateInfoKHR-oldSwapchain-parameter ] | MessageID = 0x9104e0a4
    // vkCreateSwapchainKHR(): pCreateInfo->oldSwapchain Invalid VkSwapchainKHR Object 0xd000000000d.
    // The Vulkan spec states: If oldSwapchain is not VK_NULL_HANDLE, oldSwapchain must be a valid VkSwapchainKHR handle (https://vulkan.lunarg.com/doc/view/1.4.321.1/windows/antora/spec/latest/chapters/VK_KHR_surface/wsi.html#VUID-VkSwapchainCreateInfoKHR-oldSwapchain-parameter)
    // cleanup();

    // Select swapchain parameters
    selectSurfaceFormat(newCI);
    selectPresentMode(newCI);
    calculateImageCount(newCI);

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
        .imageArrayLayers      = newCI.imageArrayLayers,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode      = sharingMode,
        .queueFamilyIndexCount = queueFamilyCount,
        .pQueueFamilyIndices   = queueFamilyIndices,
        .preTransform          = _supportDetails.capabilities.currentTransform,
        .compositeAlpha        = selectCompositeAlpha(),
        .presentMode           = _presentMode,
        .clipped               = newCI.bClipped ? VK_TRUE : VK_FALSE,
        .oldSwapchain          = oldSwapchain,
    };

    // Create swapchain and retrieve images
    bool bImageRecreated = false;
    if (!createSwapchainAndImages(vkSwapchainCI, bImageRecreated)) {
        return false;
    }

    // Destroy old swapchain after successfully creating the new one
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, oldSwapchain, _render->getAllocator());
    }

    // 这是错误的做法，因为vulkan无法获取当前实际使用的 present mode
    // uint32_t         presentModeCount = 0;
    // VkPresentModeKHR presentMode{};
    // vkGetPhysicalDeviceSurfacePresentModesKHR(_render->getPhysicalDevice(),
    //                                           _render->getSurface(),
    //                                           &presentModeCount,
    //                                           &presentMode);

    // std::string currentPresentModeStr = std::to_string(presentMode);
    // YA_CORE_INFO("Current Present Mode: {}", currentPresentModeStr);

    // if (_presentMode != presentMode)
    // {
    //     YA_CORE_ERROR("Requested present mode {}, but current present mode is {}",
    //                   std::to_string(_presentMode),
    //                   currentPresentModeStr);
    //     _presentMode = presentMode;
    //     this->bVsync = (_presentMode == VK_PRESENT_MODE_FIFO_KHR);
    // }

    // vkSwapchain
    // Set debug name
    _render->setDebugObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR,
                                m_swapChain,
                                std::format("SwapChain_{}", version).c_str());

    handleCIChanged(newCI, bImageRecreated);

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

EPresentMode::T VulkanSwapChain::getPresentMode() const
{
    return EPresentMode::fromVk(_presentMode);
}

std::vector<EPresentMode::T> VulkanSwapChain::getAvailablePresentModes() const
{
    auto                         details = VulkanSwapChainSupportDetails::query(_render->getPhysicalDevice(), _render->getSurface());
    std::vector<EPresentMode::T> modes;
    for (const auto &mode : details.presentModes) {
        modes.push_back(EPresentMode::fromVk(mode));
    }
    return modes;
}

} // namespace ya
