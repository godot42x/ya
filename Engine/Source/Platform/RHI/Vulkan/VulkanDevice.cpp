#include "VulkanDevice.h"

#include <Core/Base.h>
#include <Core/Log.h>

#include <array>
#include <cstring>
#include <set>


#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



VkSurfaceFormatKHR SwapChainSupportDetails::ChooseSwapSurfaceFormat()
{
    // Method 1
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return {
            .format     = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    // Method 2
    for (const auto &available_format : formats)
    {
        if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_format;
        }
    }

    // Fallback, ??
    return formats[0];
}

VkPresentModeKHR SwapChainSupportDetails::ChooseSwapPresentMode()
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

    // fallback best mode
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChainSupportDetails::ChooseSwapExtent(WindowProvider *provider)
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

SwapChainSupportDetails SwapChainSupportDetails::query(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails details;

    // Capabilities
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    // Formats
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count != 0)
    {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    // PresentModes
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count != 0)
    {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}


void VulkanState::create_instance()
{
    if (m_EnableValidationLayers && !is_validation_layers_supported()) {
        NE_CORE_ASSERT(false, "validation layers requested, but not available!");
    }

    VkApplicationInfo app_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Neon Engine",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .apiVersion         = VK_API_VERSION_1_2,
    };

    std::vector<const char *> extensions = onGetRequiredExtensions.ExecuteIfBound();
    if (m_EnableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo instance_create_info{
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pApplicationInfo = &app_info,
        // .enabledLayerCount       = 0,
        // .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (m_EnableValidationLayers) {
        const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = getDebugMessengerCreateInfoExt();

        instance_create_info.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
        instance_create_info.ppEnabledLayerNames = m_ValidationLayers.data();
        instance_create_info.pNext               = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_messenger_create_info;
    }
    else {
        instance_create_info.enabledLayerCount = 0;
        instance_create_info.pNext             = nullptr;
    }

    VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_Instance);
    if (result != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create instance!");
    }
}


// Assign the m_physicalDevice, m_PresentQueue, m_GraphicsQueue
void VulkanState::createLogicDevice()
{
    QueueFamilyIndices family_indices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queue_crate_infos;
    std::set<uint32_t>                   unique_queue_families = {
        static_cast<uint32_t>(family_indices.graphicsFamilyIdx),
        static_cast<uint32_t>(family_indices.supportedFamilyIdx)};

    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families) {
        queue_crate_infos.push_back({
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .queueFamilyIndex = queue_family,
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    VkPhysicalDeviceFeatures device_features{
        .samplerAnisotropy = VK_TRUE,
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .queueCreateInfoCount    = static_cast<uint32_t>(queue_crate_infos.size()),
        .pQueueCreateInfos       = queue_crate_infos.data(),
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(m_DeviceExtensions.size()),
        .ppEnabledExtensionNames = m_DeviceExtensions.data(),
        .pEnabledFeatures        = &device_features

    };
    if (m_EnableValidationLayers)
    {
        deviceCreateInfo.enabledLayerCount   = static_cast<uint32_t>(m_ValidationLayers.size());
        deviceCreateInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    }


    VkResult ret = vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_LogicalDevice);
    NE_ASSERT(ret == VK_SUCCESS, "failed to create logical device!");

    vkGetDeviceQueue(m_LogicalDevice, family_indices.supportedFamilyIdx, 0, &m_PresentQueue);
    vkGetDeviceQueue(m_LogicalDevice, family_indices.graphicsFamilyIdx, 0, &m_GraphicsQueue);
    NE_ASSERT(m_PresentQueue != VK_NULL_HANDLE, "failed to get present queue!");
    NE_ASSERT(m_GraphicsQueue != VK_NULL_HANDLE, "failed to get graphics queue!");
}

void VulkanState::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    VkCommandPoolCreateInfo poolInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = static_cast<uint32_t>(queueFamilyIndices.graphicsFamilyIdx),
    };

    if (vkCreateCommandPool(m_LogicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create command pool!");
    }
}

void VulkanState::createCommandBuffers()
{
    m_commandBuffers.resize(m_swapChain.getImages().size());

    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = m_commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size()),
    };

    if (vkAllocateCommandBuffers(m_LogicalDevice, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to allocate command buffers!");
    }
}

void VulkanState::createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    if (vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create semaphores!");
    }
}



void VulkanState::createDepthResources()
{

    VkFormat depthFormat = VulkanUtils::findSupportedImageFormat(
        m_PhysicalDevice,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);


    auto ext = m_swapChain.getExtent();
    VulkanUtils::createImage(
        m_LogicalDevice,
        m_PhysicalDevice,
        ext.width,
        ext.height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_depthImage,
        m_depthImageMemory);


    m_depthImageView = VulkanUtils::createImageView(m_LogicalDevice,
                                                    m_depthImage,
                                                    depthFormat,
                                                    VK_IMAGE_ASPECT_DEPTH_BIT);

    VulkanUtils::transitionImageLayout(m_LogicalDevice,
                                       m_commandPool,
                                       m_GraphicsQueue,
                                       m_depthImage,
                                       depthFormat,
                                       VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}
