#include "VulkanDevice.h"

#include <Core/Base.h>
#include <Core/Log.h>

#include <array>
#include <cstring>
#include <set>


#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



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

    // Load debug function pointers after instance creation
    loadDebugFunctionPointers();
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

void VulkanState::setupDebugMessengerExt()
{
    if (!m_EnableValidationLayers) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = getDebugMessengerCreateInfoExt();

    if (pfnCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessengerCallback) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to set up debug messenger!");
    }
}

void VulkanState::setupReportCallbackExt()
{

    if (!m_EnableValidationLayers) {
        return;
    }

    VkDebugReportCallbackCreateInfoEXT createInfo{
        .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = [](VkDebugReportFlagsEXT      flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t                   object,
                          size_t                     location,
                          int32_t                    messageCode,
                          const char                *pLayerPrefix,
                          const char                *pMessage,
                          void                      *pUserData) -> VkBool32 {
            NE_CORE_ERROR("[Validation Layer] {}: {}", pLayerPrefix, pMessage);
            return VK_FALSE;
        },
    };

    if (pfnCreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, &m_DebugReportCallback) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to set up debug report callback!");
    }
}

void VulkanState::destroyDebugCallBackExt()
{
    if (m_EnableValidationLayers) {
        pfnDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessengerCallback, nullptr);
    }
}

void VulkanState::destroyDebugReportCallbackExt()
{
    if (m_EnableValidationLayers) {
        pfnDestroyDebugReportCallbackEXT(m_Instance, m_DebugReportCallback, nullptr);
        m_DebugReportCallback = VK_NULL_HANDLE;
    }
}


bool VulkanState::is_device_suitable(VkPhysicalDevice device)
{
    // VkPhysicalDeviceProperties deviceProperties;
    // vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // VkPhysicalDeviceFeatures devicesFeatures;
    // vkGetPhysicalDeviceFeatures(device, &devicesFeatures);

    // return (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) && devicesFeatures.geometryShader;

    QueueFamilyIndices indices = QueueFamilyIndices::query(
        m_Surface, device, VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT);

    bool bExtensionSupported = is_device_extension_support(device);

    bool bSwapchainComplete = false;
    if (bExtensionSupported)
    {
        VulkanSwapChainSupportDetails swapchain_support_details = VulkanSwapChainSupportDetails::query(device, m_Surface);

        bSwapchainComplete = !swapchain_support_details.formats.empty() &&
                             !swapchain_support_details.present_modes.empty();
    }

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(device, &supported_features);


    return indices.is_complete() &&
           bExtensionSupported &&
           bSwapchainComplete &&
           // TODO: other feature that we required
           supported_features.samplerAnisotropy; // bool
}

bool VulkanState::is_validation_layers_supported()
{
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const char *layer : m_ValidationLayers)
    {
        NE_CORE_DEBUG("Checking validation layer: {}", layer);
        bool ok = false;
        for (const auto &layer_properties : layers)
        {
            if (0 != std::strcmp(layer, layer_properties.layerName)) {
                return false;
            }
        }
    }
    return true;
}

bool VulkanState::is_device_extension_support(VkPhysicalDevice device)
{
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available_extensions.data());

    // Global predefine extensions that we need
    std::set<std::string> required_extensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

    for (const auto &extension : available_extensions)
    {
        required_extensions.erase(extension.extensionName);
    }
    return required_extensions.empty();
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

void VulkanState::loadDebugFunctionPointers()
{
    if (m_EnableValidationLayers) {
        // Load debug utils messenger functions
        pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
        pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        
        // Load debug report callback functions (deprecated but still used)
        pfnCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
        pfnDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");
        
        NE_CORE_ASSERT(pfnCreateDebugUtilsMessengerEXT != nullptr, "Failed to load vkCreateDebugUtilsMessengerEXT");
        NE_CORE_ASSERT(pfnDestroyDebugUtilsMessengerEXT != nullptr, "Failed to load vkDestroyDebugUtilsMessengerEXT");
        NE_CORE_ASSERT(pfnCreateDebugReportCallbackEXT != nullptr, "Failed to load vkCreateDebugReportCallbackEXT");
        NE_CORE_ASSERT(pfnDestroyDebugReportCallbackEXT != nullptr, "Failed to load vkDestroyDebugReportCallbackEXT");
    }
}
