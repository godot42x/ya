#include "VulkanRender.h"


#include <Core/Base.h>
#include <Core/Log.h>

#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vk_enum_string_helper.h>

#ifdef _WIN32
    #include <windows.h>
#endif



#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



void VulkanRender::createInstance()
{
    // Check supported Vulkan API version
    uint32_t supportedVersion = 0;
    VkResult result           = vkEnumerateInstanceVersion(&supportedVersion);
    if (result == VK_SUCCESS) {
        uint32_t major = VK_VERSION_MAJOR(supportedVersion);
        uint32_t minor = VK_VERSION_MINOR(supportedVersion);
        uint32_t patch = VK_VERSION_PATCH(supportedVersion);
        NE_CORE_INFO("Supported Vulkan API version:{} {}.{}.{}", supportedVersion, major, minor, patch);
    }
    NE_CORE_ASSERT(supportedVersion >= VK_API_VERSION_1_0, "Vulkan API version 1.0 or higher is required!");
    if (supportedVersion < VK_API_VERSION_1_1) {
        supportedVersion = VK_API_VERSION_1_0;
    }
    else if (supportedVersion < VK_API_VERSION_1_2) {
        supportedVersion = VK_API_VERSION_1_1;
    }
    else if (supportedVersion < VK_API_VERSION_1_3) {
        supportedVersion = VK_API_VERSION_1_2;
    }
    else {
        supportedVersion = VK_API_VERSION_1_3;
    }
    // supportedVersion = VK_API_VERSION_1_2;

    NE_CORE_INFO("Using Vulkan API version: {}.{}.{}", VK_VERSION_MAJOR(supportedVersion), VK_VERSION_MINOR(supportedVersion), VK_VERSION_PATCH(supportedVersion));

    VkApplicationInfo appInfo{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Neon Engine",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .apiVersion         = supportedVersion,
    };

    auto requestExtensions = _instanceExtensions;
    auto requestLayers     = _instanceLayers;

    const auto &required = onGetRequiredInstanceExtensions.ExecuteIfBound();
    for (const auto &ext : required) {
        requestExtensions.push_back(ext);
    }
    if (m_EnableValidationLayers)
    {
        requestLayers.insert(requestLayers.end(),
                             _instanceValidationLayers.begin(),
                             _instanceValidationLayers.end());
        requestExtensions.push_back({VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true});
    }

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    std::vector<const char *> extensionNames;
    std::vector<const char *> layerNames;

    bool bSupported = isFeatureSupported(
        "Vulkan instance",
        availableExtensions,
        availableLayers,
        requestExtensions,
        requestLayers,
        extensionNames,
        layerNames);
    NE_CORE_ASSERT(bSupported, "Required feature not supported!");



    VkInstanceCreateInfo instanceCI{
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<uint32_t>(layerNames.size()),
        .ppEnabledLayerNames     = layerNames.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(extensionNames.size()),
        .ppEnabledExtensionNames = extensionNames.data(),
    };

    if (m_EnableValidationLayers) {
        const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = getDebugMessengerCreateInfoExt();

        instanceCI.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_messenger_create_info;
    }

    NE_CORE_INFO("About to call vkCreateInstance...");
    result = vkCreateInstance(&instanceCI, nullptr, &_instance);
    NE_CORE_ASSERT(result == VK_SUCCESS, "failed to create instance! Result: {} {}", static_cast<int32_t>(result), string_VkResult(result));

    NE_CORE_INFO("Vulkan instance created successfully!");

    // Load debug function pointers after instance creation
    // loadDebugFunctionPointers();
}

void VulkanRender::findPhysicalDevice()
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // Enumerate physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
    NE_CORE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
    NE_CORE_INFO("Found {} physical devices", deviceCount);

    auto getDeviceTypeStr = [](VkPhysicalDeviceType type) -> std::string {
        switch (type) {
            CASE_ENUM_TO_STR(VK_PHYSICAL_DEVICE_TYPE_OTHER);
            CASE_ENUM_TO_STR(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
            CASE_ENUM_TO_STR(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            CASE_ENUM_TO_STR(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU);
            CASE_ENUM_TO_STR(VK_PHYSICAL_DEVICE_TYPE_CPU);
        default:
            return std::format("Unknown device type: {}", (int)type);
        }
    };

    auto getDeviceScore = [](VkPhysicalDevice device) -> int {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        int score = 0;

        switch (deviceProperties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score += 1000;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score += 500;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score += 100;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            score += 50;
            break;
        default:
            break;
        }

        // Check for geometry shader support
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        if (deviceFeatures.geometryShader) {
            score += 100;
        }

        return score;
    };

    // Select the first suitable device
    int maxScore = 0;

    VkPhysicalDeviceProperties deviceProperties;
    for (const auto &device : devices) {
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        printf("==========================================\n");
        NE_CORE_INFO("Found device: {} {}", deviceProperties.deviceName, (uintptr_t)device);
        NE_CORE_INFO("Device type: {}", getDeviceTypeStr(deviceProperties.deviceType));
        NE_CORE_INFO("Vendor ID: {}", deviceProperties.vendorID);
        NE_CORE_INFO("Device ID: {}", deviceProperties.deviceID);
        NE_CORE_INFO("API version: {}.{}.{}", VK_VERSION_MAJOR(deviceProperties.apiVersion), VK_VERSION_MINOR(deviceProperties.apiVersion), VK_VERSION_PATCH(deviceProperties.apiVersion));


        int score = getDeviceScore(device);
        NE_CORE_INFO("Device score: {}", score);

        // surface format support
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, formats.data());
        for (const auto &format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                score += 100; // Add score for preferred format
                break;
            }
        }

        // surface present mode support
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        NE_CORE_INFO("Queue family count: {}", count);

        printf("==========================================\n");

        if (score < maxScore) {
            continue; // Skip if score is not better than max
        }

        std::vector<QueueFamilyIndices> graphicsQueueFamilies;
        std::vector<QueueFamilyIndices> presentQueueFamilies;

        int32_t familyIndex = 0;
        for (const auto &queueFamily : families) {
            if (queueFamily.queueCount == 0)
            {
                continue;
            }

            // required graphics rendering queue
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamilies.push_back({
                    familyIndex,
                    (int32_t)queueFamily.queueCount,
                });
            }

            // also need support for presentation queue
            VkBool32 bSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, familyIndex, _surface, &bSupport);
            if (bSupport)
            {
                presentQueueFamilies.push_back({
                    familyIndex,
                    (int32_t)queueFamily.queueCount,
                });
            }

            ++familyIndex;
        }

        // better graphics and present queue use different queue
        if (graphicsQueueFamilies.size() > 0 && presentQueueFamilies.size() > 0)
        {
            maxScore       = score;
            physicalDevice = device;

            if (graphicsQueueFamilies.size() > 1 || presentQueueFamilies.size() > 1)
            {
                for (const auto &graphicsQueueFamily : graphicsQueueFamilies)
                {
                    for (const auto &presentQueueFamily : presentQueueFamilies)
                    {
                        if (graphicsQueueFamily.queueFamilyIndex != presentQueueFamily.queueFamilyIndex)
                        {
                            _graphicsQueueFamily = graphicsQueueFamily;
                            _presentQueueFamily  = presentQueueFamily;
                            break;
                        }
                    }
                }
            }
            else {
                _graphicsQueueFamily = graphicsQueueFamilies[0];
                _presentQueueFamily  = presentQueueFamilies[0];
            }
        }
    }

    NE_CORE_INFO("Selected physical device: {}", (uintptr_t)physicalDevice);
    NE_CORE_INFO("Graphics queue idx: {} count: {}", _graphicsQueueFamily.queueFamilyIndex, _graphicsQueueFamily.queueCount);
    NE_CORE_INFO("Present queue idx {} count: {}", _presentQueueFamily.queueFamilyIndex, _presentQueueFamily.queueCount);
    m_PhysicalDevice = physicalDevice;
}

void VulkanRender::createSurface()
{
    bool ok = onCreateSurface.ExecuteIfBound(&(*_instance), &_surface);
    NE_CORE_ASSERT(ok, "Failed to create surface!");
}

bool VulkanRender::createLogicDevice(uint32_t graphicsQueueCount, uint32_t presentQueueCount, const LogicDeviceSettings &settings)
{

    if (graphicsQueueCount > _graphicsQueueFamily.queueCount)
    {
        NE_CORE_ERROR("Requested graphics queue count {} exceeds available queue count {} for family index {}",
                      graphicsQueueCount,
                      _graphicsQueueFamily.queueCount,
                      _graphicsQueueFamily.queueFamilyIndex);
        return false;
    }

    if (presentQueueCount > _presentQueueFamily.queueCount)
    {
        NE_CORE_ERROR("Requested present queue count {} exceeds available queue count {} for family index {}",
                      presentQueueCount,
                      _presentQueueFamily.queueCount,
                      _presentQueueFamily.queueFamilyIndex);
        return false;
    }

    // all graphics queue need lower than present queue
    std::vector<float> graphicsQueuePriorities(graphicsQueueCount, 0.0f);
    std::vector<float> presentQueuePriorities(presentQueueCount, 1.0f);

    bool bSameQueueFamily = isSameQueueFamily();


    std::vector<VkDeviceQueueCreateInfo> deviceQueueCIs;
    deviceQueueCIs.push_back({
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = static_cast<uint32_t>(_graphicsQueueFamily.queueFamilyIndex),
        .queueCount       = graphicsQueueCount,
        .pQueuePriorities = graphicsQueuePriorities.data(),
    });
    if (bSameQueueFamily)
    {
        auto &graphicsQueueCI      = deviceQueueCIs.back();
        graphicsQueueCI.queueCount = std::min(graphicsQueueCount, graphicsQueueCount + presentQueueCount);
        graphicsQueuePriorities.insert(graphicsQueuePriorities.end(), presentQueuePriorities.begin(), presentQueuePriorities.end());
        graphicsQueueCI.pQueuePriorities = graphicsQueuePriorities.data();
    }
    else
    {
        deviceQueueCIs.push_back({
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .queueFamilyIndex = static_cast<uint32_t>(_presentQueueFamily.queueFamilyIndex),
            .queueCount       = presentQueueCount,
            .pQueuePriorities = presentQueuePriorities.data(),
        });
    }


    auto requestExtensions = _deviceExtensions;
    auto requestLayers     = _deviceLayers;


    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());

    uint32_t layerCount = 0;
    vkEnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, availableLayers.data());

    std::vector<const char *> extensionNames;
    std::vector<const char *> layerNames;


    bool bSupported = isFeatureSupported(
        "Vulkan device",
        availableExtensions,
        availableLayers,
        requestExtensions,
        requestLayers,
        extensionNames,
        layerNames);
    if (!bSupported)
    {
        NE_CORE_ERROR("Vulkan instance is not suitable!");
        return false;
    }

    VkPhysicalDeviceFeatures physicalDeviceFeatures = {
        .samplerAnisotropy = VK_TRUE, // Enable anisotropic filtering
        // .geometryShader    = VK_TRUE, // Enable geometry shader support
        // .shaderClipDistance = VK_TRUE, // Enable clip distance support
        // .shaderCullDistance = VK_TRUE, // Enable cull distance support
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .queueCreateInfoCount    = static_cast<uint32_t>(deviceQueueCIs.size()),
        .pQueueCreateInfos       = deviceQueueCIs.data(),
        .enabledLayerCount       = static_cast<uint32_t>(layerNames.size()),
        .ppEnabledLayerNames     = layerNames.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(extensionNames.size()),
        .ppEnabledExtensionNames = extensionNames.data(),
        .pEnabledFeatures        = &physicalDeviceFeatures,

    };


    VkResult ret = vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_LogicalDevice);
    NE_ASSERT(ret == VK_SUCCESS, "failed to create logical device!");

    for (int i = 0; i < graphicsQueueCount; i++)
    {
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_LogicalDevice, _graphicsQueueFamily.queueFamilyIndex, i, &queue);
        if (queue == VK_NULL_HANDLE) {
            NE_CORE_ERROR("Failed to get graphics queue!");
            return false;
        }

        _graphicsQueues.push_back(VulkanQueue(_graphicsQueueFamily.queueFamilyIndex, i, queue, false));
    }
    for (int i = 0; i < presentQueueCount; i++)
    {
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_LogicalDevice, _presentQueueFamily.queueFamilyIndex, i, &queue);
        if (queue == VK_NULL_HANDLE) {
            NE_CORE_ERROR("Failed to get present queue!");
            return false;
        }
        _presentQueues.push_back(VulkanQueue(_presentQueueFamily.queueFamilyIndex, i, queue, true));
    }

    return true;
}

bool VulkanRender::createCommandPool()
{
    // QueueFamilyIndices queueFamilyIndices = QueueFamilyIndices::query(m_Surface, m_PhysicalDevice);

    VkCommandPoolCreateInfo graphicsCommandPoolCI{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = static_cast<uint32_t>(_graphicsQueueFamily.queueFamilyIndex),
    };
    if (vkCreateCommandPool(m_LogicalDevice, &graphicsCommandPoolCI, nullptr, &_graphicsCommandPool) != VK_SUCCESS) {
        NE_CORE_ERROR("failed to create graphics command pool!");
        return false;
    }

    if (isSameQueueFamily()) {
        VkCommandPoolCreateInfo presentCommandPoolCI{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = static_cast<uint32_t>(_presentQueueFamily.queueFamilyIndex),
        };

        if (vkCreateCommandPool(m_LogicalDevice, &presentCommandPoolCI, nullptr, &_presentCommandPool) != VK_SUCCESS) {
            NE_CORE_ERROR("failed to create present command pool!");
            return false;
        }
    }

    return true;
}

void VulkanRender::createCommandBuffers()
{
    UNIMPLEMENTED();
    m_commandBuffers.resize(m_swapChain.getImages().size());

    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = _graphicsCommandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size()),
    };

    if (vkAllocateCommandBuffers(m_LogicalDevice, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to allocate command buffers!");
    }
}

void VulkanRender::createSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    if (vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_LogicalDevice, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create semaphores!");
    }
}

// void VulkanRender::setupDebugMessengerExt()
// {
//     if (!m_EnableValidationLayers) {
//         return;
//     }


//     VkDebugUtilsMessengerCreateInfoEXT createInfo = getDebugMessengerCreateInfoExt();

//     // if (pfnCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessengerCallback) != VK_SUCCESS) {
//     //     NE_CORE_ASSERT(false, "failed to set up debug messenger!");
//     // }
//     if (vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessengerCallback) != VK_SUCCESS) {
//         NE_CORE_ASSERT(false, "failed to set up debug messenger!");
//     }
//     NE_CORE_INFO("Debug messenger setup successfully");
// }

// void VulkanRender::setupReportCallbackExt()
// {
//     if (!m_EnableValidationLayers) {
//         return;
//     }
//     // if (!pfnCreateDebugReportCallbackEXT) {
//     //     NE_CORE_WARN("pfnCreateDebugReportCallbackEXT is not loaded, skipping debug report callback setup");
//     //     return;
//     // }

//     VkDebugReportCallbackCreateInfoEXT createInfo{
//         .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
//         .flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
//         .pfnCallback = [](VkDebugReportFlagsEXT      flags,
//                           VkDebugReportObjectTypeEXT objectType,
//                           uint64_t                   object,
//                           size_t                     location,
//                           int32_t                    messageCode,
//                           const char                *pLayerPrefix,
//                           const char                *pMessage,
//                           void                      *pUserData) -> VkBool32 {
//             NE_CORE_ERROR("[Validation Layer] {}: {}", pLayerPrefix, pMessage);
//             return VK_FALSE;
//         },
//     };

//     // if (pfnCreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, &m_DebugReportCallback) != VK_SUCCESS) {
//     //     NE_CORE_ASSERT(false, "failed to set up debug report callback!");
//     // }
//     if (vkCreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, &m_DebugReportCallback) != VK_SUCCESS) {
//         NE_CORE_ASSERT(false, "failed to set up debug report callback!");
//     }

//     NE_CORE_INFO("Debug report callback setup successfully");
// }

// void VulkanRender::destroyDebugCallBackExt()
// {
//     if (m_EnableValidationLayers) {
//         // pfnDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessengerCallback, nullptr);
//         vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessengerCallback, nullptr);
//     }
// }

// void VulkanRender::destroyDebugReportCallbackExt()
// {
//     if (m_EnableValidationLayers) {
//         // pfnDestroyDebugReportCallbackEXT(m_Instance, m_DebugReportCallback, nullptr);
//         vkDestroyDebugReportCallbackEXT(m_Instance, m_DebugReportCallback, nullptr);
//         m_DebugReportCallback = VK_NULL_HANDLE;
//     }
// }



bool VulkanRender::isFeatureSupported(
    std::string_view                          contextStr,
    const std::vector<VkExtensionProperties> &availableExtensions,
    const std::vector<VkLayerProperties>     &availableLayers,
    const std::vector<DeviceFeature>         &requestExtensions,
    const std::vector<DeviceFeature>         &requestLayers,
    std::vector<const char *>                &outExtensionNames,
    std::vector<const char *>                &outLayerNames)
{

    NE_CORE_INFO("=================================");
    NE_CORE_INFO("Available {} layers:", contextStr);
    for (size_t i = 0; i < availableLayers.size(); i += 3) {
        std::string line = "";
        for (size_t j = i; j < std::min(i + 3, availableLayers.size()); ++j) {
            line += std::format("\t| {:<45} ", availableLayers[j].layerName);
        }
        NE_CORE_INFO("{}", line);
    }
    NE_CORE_INFO("Available {} extensions:", contextStr);
    for (size_t i = 0; i < availableExtensions.size(); i += 3) {
        std::string line = "  ";
        for (size_t j = i; j < std::min(i + 3, availableExtensions.size()); ++j) {
            line += std::format("| {:<45} ", availableExtensions[j].extensionName);
        }
        NE_CORE_INFO("{}", line);
    }


    // Clear the output vectors first
    outExtensionNames.clear();
    outLayerNames.clear();

    for (const auto &feat : requestExtensions) {
        if (std::find(outExtensionNames.begin(), outExtensionNames.end(), feat.name.c_str()) == outExtensionNames.end() &&
            !std::any_of(availableExtensions.begin(),
                         availableExtensions.end(),
                         [&feat](const VkExtensionProperties &ext) {
                             return std::strcmp(ext.extensionName, feat.name.c_str()) == 0;
                         }))
        {
            NE_CORE_WARN("Extension {} is not supported by the {}", feat.name, contextStr);
            if (feat.bRequired) {
                return false; // If it's a required extension, return false
            }
            continue;
        }
        outExtensionNames.push_back(feat.name.c_str());
    }

    for (const auto &feat : requestLayers) {
        if (std::find(outLayerNames.begin(), outLayerNames.end(), feat.name.c_str()) == outLayerNames.end() &&
            !std::any_of(
                availableLayers.begin(),
                availableLayers.end(),
                [&feat](const VkLayerProperties &layer) {
                    return std::string(layer.layerName) == feat.name;
                }))
        {

            NE_CORE_WARN("Layer {} is not supported by the {}", feat.name, contextStr);
            if (feat.bRequired) {
                return false; // If it's a required layer, return false
            }
            continue;
        }
        // Debug log to ensure we're adding the correct layer name
        outLayerNames.push_back(feat.name.c_str());
    }

    NE_CORE_INFO("=================================");
    NE_CORE_INFO("Final Extension Names({}):", outExtensionNames.size());
    for (size_t i = 0; i < outExtensionNames.size(); ++i) {
        NE_CORE_INFO("  Final Extension[{}]: {}", i, outExtensionNames[i]);
    }
    NE_CORE_INFO("Final Layer Names({}):", outLayerNames.size());
    for (size_t i = 0; i < outLayerNames.size(); ++i) {
        NE_CORE_INFO("  Final Layer[{}]: {}", i, outLayerNames[i]);
    }


    return true;
}



// void VulkanRender::createDepthResources()
// {

//     VkFormat depthFormat = VulkanUtils::findSupportedImageFormat(
//         m_PhysicalDevice,
//         {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
//         VK_IMAGE_TILING_OPTIMAL,
//         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);


//     auto ext = m_swapChain.getExtent();

//     VulkanUtils::createImage(
//         m_LogicalDevice,
//         m_PhysicalDevice,
//         ext.width,
//         ext.height,
//         depthFormat,
//         VK_IMAGE_TILING_OPTIMAL,
//         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
//         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//         m_depthImage,
//         m_depthImageMemory);


//     m_depthImageView = VulkanUtils::createImageView(m_LogicalDevice,
//                                                     m_depthImage,
//                                                     depthFormat,
//                                                     VK_IMAGE_ASPECT_DEPTH_BIT);

//     VulkanUtils::transitionImageLayout(m_LogicalDevice,
//                                        m_commandPool,
//                                        m_GraphicsQueue,
//                                        m_depthImage,
//                                        depthFormat,
//                                        VK_IMAGE_LAYOUT_UNDEFINED,
//                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
// }

// void VulkanRender::loadDebugFunctionPointers()
// {
//     if (m_EnableValidationLayers) {
//         // Load debug utils messenger functions
//         pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
//             vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
//         pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
//             vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");

//         // Load debug report callback functions (deprecated but still used)
//         pfnCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
//             vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
//         pfnDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
//             vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");

//         if (!pfnCreateDebugUtilsMessengerEXT) {
//             NE_CORE_WARN("pfnCreateDebugUtilsMessengerEXT is not loaded, skipping debug utils messenger setup");
//             pfnCreateDebugUtilsMessengerEXT = nullptr; // Set to nullptr to avoid using it later
//         }
//         if (!pfnCreateDebugReportCallbackEXT) {
//             NE_CORE_WARN("pfnCreateDebugReportCallbackEXT is not loaded, skipping debug report callback setup");
//             pfnCreateDebugReportCallbackEXT = nullptr; // Set to nullptr to avoid using it later
//         }
//         if (!pfnDestroyDebugUtilsMessengerEXT) {
//             NE_CORE_WARN("pfnDestroyDebugUtilsMessengerEXT is not loaded, skipping debug utils messenger cleanup");
//             pfnDestroyDebugUtilsMessengerEXT = nullptr; // Set to nullptr to avoid using it later
//         }
//         if (!pfnDestroyDebugReportCallbackEXT) {
//             NE_CORE_WARN("pfnDestroyDebugReportCallbackEXT is not loaded, skipping debug report callback cleanup");
//             pfnDestroyDebugReportCallbackEXT = nullptr; // Set to nullptr to avoid using it later
//         }
//     }
// }

/*
void VulkanRender::createVertexBuffer()
{
    // Define triangle vertices (centered triangle)
    m_triangleVertices = {
        {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Bottom vertex (red)
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}, // Top left vertex (blue)
        {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // Top right vertex (green)
    };

    VkDeviceSize bufferSize = sizeof(m_triangleVertices[0]) * m_triangleVertices.size();

    // Create staging buffer
    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    // Copy vertex data to staging buffer
    void *data;
    vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_triangleVertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);

    // Create vertex buffer
    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer,
        m_vertexBufferMemory);

    // Copy from staging buffer to vertex buffer
    VulkanUtils::copyBuffer(m_LogicalDevice,
                            m_commandPool,
                            m_GraphicsQueue,
                            stagingBuffer,
                            m_vertexBuffer,
                            bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
}

void VulkanRender::createUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(CameraData);

    VulkanUtils::createBuffer(
        m_LogicalDevice,
        m_PhysicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_uniformBuffer,
        m_uniformBufferMemory);

    // Map the uniform buffer memory so we can update it
    vkMapMemory(m_LogicalDevice, m_uniformBufferMemory, 0, bufferSize, 0, &m_uniformBufferMapped);
}

void VulkanRender::updateUniformBuffer()
{
    CameraData ubo{};
    ubo.viewProjection = glm::mat4(1.0f); // Identity matrix for now (no transformation)

    memcpy(m_uniformBufferMapped, &ubo, sizeof(ubo));
}

void VulkanRender::drawTriangle()
{
    // Wait for previous frame
    vkWaitForFences(m_LogicalDevice, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_LogicalDevice, 1, &m_inFlightFence);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = m_swapChain.acquireNextImage(imageIndex, m_imageAvailableSemaphore);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        NE_CORE_ASSERT(false, "Failed to acquire swap chain image!");
    }

    // Update uniform buffer
    updateUniformBuffer();

    // Reset and begin command buffer
    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to begin recording command buffer!");
    }

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // renderPassInfo.renderPass        = m_renderPass.getRenderPass();
    // renderPassInfo.framebuffer       = m_renderPass.getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChain.getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{1.0f, 0.0f, 1.0f, 1.0f}}; // Clear color: black
    clearValues[1].depthStencil = {1.0f, 0};                  // Clear depth to 1.0

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind graphics pipeline - use the first available pipeline or a specific one
    FName pipelineName = "DefaultTriangle"; // Default name
    if (!m_pipelineManager.hasPipeline(pipelineName)) {
        // Get the first available pipeline
        auto pipelineNames = m_pipelineManager.getPipelineNames();
        if (!pipelineNames.empty()) {
            pipelineName = pipelineNames[0];
        }
        else {
            NE_CORE_ERROR("No pipelines available for rendering!");
            return;
        }
    }

    if (!m_pipelineManager.bindPipeline(commandBuffer, pipelineName)) {
        NE_CORE_ERROR("Failed to bind pipeline: {}", pipelineName);
        return;
    }

    // Bind descriptor sets for the active pipeline
    m_pipelineManager.bindDescriptorSets(commandBuffer, pipelineName);

#if 1 // need to set the vulkan dynamic state, see VkPipelineDynamicStateCreateInfo
    // Set viewport
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapChain.getExtent().width);
    viewport.height   = static_cast<float>(m_swapChain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);


    // Set scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChain.getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
#endif

    // Bind vertex buffer
    VkBuffer     vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[]       = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Draw triangle
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_triangleVertices.size()), 1, 0, 0);


#if 1 // multiple renderpass
    // vkCmdNextSubpass2()
    // vkCmdNextSubpass()
#endif

    // End render pass and command buffer
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to record command buffer!");
    }

    // Submit command buffer
    VkSemaphore          waitSemaphores[]   = {m_imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore          signalSemaphores[] = {m_renderFinishedSemaphore};


    VkSubmitInfo submitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = waitSemaphores,
        .pWaitDstStageMask    = waitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = signalSemaphores,
    };


    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_inFlightFence) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to submit draw command buffer!");
    }

    // Present
    result = m_swapChain.presentImage(imageIndex, m_renderFinishedSemaphore, m_PresentQueue);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "Failed to present swap chain image!");
    }
}
    */

void VulkanRender::recreateSwapChain()
{
    vkDeviceWaitIdle(m_LogicalDevice);

    // Clean up depth resources
    // if (m_depthImage) {
    //     vkDestroyImage(m_LogicalDevice, m_depthImage, nullptr);
    //     vkDestroyImageView(m_LogicalDevice, m_depthImageView, nullptr);
    //     vkFreeMemory(m_LogicalDevice, m_depthImageMemory, nullptr);
    // }

    // Recreate swap chain
    m_swapChain.recreate();

    // Recreate depth resources with new extent
    // createDepthResources();

    // Recreate render pass framebuffers
    // m_renderPass.recreate(m_swapChain.getImageViews(), m_depthImageView, m_swapChain.getExtent());

    // Recreate all pipelines with new render pass and extent
    // m_pipelineManager.recreateAllPipelines(m_renderPass.getRenderPass(), m_swapChain.getExtent());

    NE_CORE_INFO("Swap chain and all pipelines recreated successfully");
}

void VulkanRender::createFences()
{
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT // Start in signaled state
    };

    if (vkCreateFence(m_LogicalDevice, &fenceInfo, nullptr, &m_inFlightFence) != VK_SUCCESS) {
        NE_CORE_ASSERT(false, "failed to create fence!");
    }
}
