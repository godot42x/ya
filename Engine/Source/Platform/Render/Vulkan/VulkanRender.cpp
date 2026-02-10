#include "VulkanRender.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorSet.h"

#include <Core/Base.h>
#include <Core/Log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>



#ifdef _WIN32
    #include <windows.h>
#endif



#define panic(...) YA_CORE_ASSERT(false, __VA_ARGS__);

namespace ya
{

VkObjectType toVk(ERenderObject type)
{
    switch (type) {
    case ERenderObject::Image:
        return VK_OBJECT_TYPE_IMAGE;
    // Add more cases as needed
    default:
        UNREACHABLE();
    }
    return VK_OBJECT_TYPE_MAX_ENUM;
}

// Destructor needs to be defined in .cpp where VulkanDescriptor is complete
VulkanRender::~VulkanRender()
{
    if (_descriptorHelper) {
        delete _descriptorHelper;
        _descriptorHelper = nullptr;
    }
}



void VulkanRender::createInstance()
{
    // Check supported Vulkan API version
    VkResult result = vkEnumerateInstanceVersion(&apiVersion);
    if (result == VK_SUCCESS) {
        uint32_t major = VK_VERSION_MAJOR(apiVersion);
        uint32_t minor = VK_VERSION_MINOR(apiVersion);
        uint32_t patch = VK_VERSION_PATCH(apiVersion);
        YA_CORE_INFO("Supported Vulkan API version:{} {}.{}.{}", apiVersion, major, minor, patch);
    }
    YA_CORE_ASSERT(apiVersion >= VK_API_VERSION_1_0, "Vulkan API version 1.0 or higher is required!");
    if (apiVersion < VK_API_VERSION_1_1) {
        apiVersion = VK_API_VERSION_1_0;
    }
    else if (apiVersion < VK_API_VERSION_1_2) {
        apiVersion = VK_API_VERSION_1_1;
    }
    else if (apiVersion < VK_API_VERSION_1_3) {
        apiVersion = VK_API_VERSION_1_2;
    }
    else {
        apiVersion = VK_API_VERSION_1_3;
    }
    // supportedVersion = VK_API_VERSION_1_2;

    YA_CORE_INFO("Using Vulkan API version: {}.{}.{}", VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));

    VkApplicationInfo appInfo{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "ya Engine",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = apiVersion,
    };

    std::vector<DeviceFeature> requestExtensions = _instanceExtensions;
    std::vector<DeviceFeature> requestLayers     = _instanceLayers;

    const auto &required = onGetRequiredInstanceExtensions.executeIfBound();
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
    YA_CORE_ASSERT(bSupported, "Required feature not supported!");

    if (std::find_if(extensionNames.begin(),
                     extensionNames.end(),
                     [](auto a) { return std::strcmp(a, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; }) == extensionNames.end())
    {
        YA_CORE_WARN("VK_EXT_DEBUG_UTILS_EXTENSION_NAME is not supported, some features may not work!");
    }
    else {
        this->bSupportDebugUtils = true;
    }

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
    // This lines did noting
    // if (m_EnableValidationLayers) {
    //     const VkDebugUtilsMessengerCreateInfoEXT &debug_messenger_create_info = getDebugMessengerCreateInfoExt();
    //     instanceCI.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_messenger_create_info;
    // }


    YA_CORE_INFO("About to call vkCreateInstance...");
    result = vkCreateInstance(&instanceCI, getAllocator(), &_instance);
    YA_CORE_ASSERT(result == VK_SUCCESS, "failed to create instance! Result: {} {}", static_cast<int32_t>(result), result);

    YA_CORE_INFO("Vulkan instance created successfully!");
}

void VulkanRender::findPhysicalDevice()
{
    m_PhysicalDevice     = VK_NULL_HANDLE;
    _graphicsQueueFamily = {};
    _presentQueueFamily  = {};
    _deviceCandidates.clear();

    uint32_t deviceCount = 0;
    VK_CALL(vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr));
    YA_CORE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CALL(vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data()));
    YA_CORE_INFO("Found {} physical devices", deviceCount);

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

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        if (deviceFeatures.geometryShader) {
            score += 100;
        }

        return score;
    };

    for (const auto &device : devices) {
        PhysicalDeviceCandidate candidate;

        candidate.device = device;
        vkGetPhysicalDeviceProperties(device, &candidate.properties);

        printf("==========================================\n");
        YA_CORE_INFO("Found device: {} {}", candidate.properties.deviceName, (uintptr_t)device);
        YA_CORE_INFO("Device type: {}", getDeviceTypeStr(candidate.properties.deviceType));
        YA_CORE_INFO("Vendor ID: {}", candidate.properties.vendorID);
        YA_CORE_INFO("Device ID: {}", candidate.properties.deviceID);
        YA_CORE_INFO("API version: {}.{}.{}",
                     VK_VERSION_MAJOR(candidate.properties.apiVersion),
                     VK_VERSION_MINOR(candidate.properties.apiVersion),
                     VK_VERSION_PATCH(candidate.properties.apiVersion));

        candidate.score = getDeviceScore(device);

        uint32_t formatCount = 0;
        VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr));
        if (formatCount > 0) {
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, formats.data()));
            for (const auto &format : formats) {
                if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    candidate.score += 100;
                    break;
                }
            }
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        candidate.queueFamilyCount = familyCount;

        YA_CORE_INFO("Device score: {}", candidate.score);
        YA_CORE_INFO("Queue family count: {}", familyCount);

        std::vector<QueueFamilyIndices> graphicsQueueFamilies;
        std::vector<QueueFamilyIndices> presentQueueFamilies;

        int32_t familyIndex = 0;
        for (const auto &queueFamily : families) {
            if (queueFamily.queueCount == 0) {
                ++familyIndex;
                continue;
            }

            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                YA_CORE_TRACE("\tGraphics queue family index: {}:{}, queue count: {}", graphicsQueueFamilies.size(), familyIndex, queueFamily.queueCount);
                graphicsQueueFamilies.push_back({
                    .queueFamilyIndex = familyIndex,
                    .queueCount       = (int32_t)queueFamily.queueCount,
                });
            }

            VkBool32 bSupport = false;
            VK_CALL(vkGetPhysicalDeviceSurfaceSupportKHR(device, familyIndex, _surface, &bSupport));
            if (bSupport) {
                YA_CORE_TRACE("\tPresent queue family index: {}:{}, queue count: {}", presentQueueFamilies.size(), familyIndex, queueFamily.queueCount);
                presentQueueFamilies.push_back({
                    .queueFamilyIndex = familyIndex,
                    .queueCount       = (int32_t)queueFamily.queueCount,
                });
            }

            ++familyIndex;
        }

        printf("==========================================\n");

        if (graphicsQueueFamilies.empty() || presentQueueFamilies.empty()) {
            YA_CORE_WARN("Skipping device {}, missing required queue families", candidate.properties.deviceName);
            continue;
        }


        bool foundSeparate = false;
        for (const auto &graphicsQueueFamily : graphicsQueueFamilies) {
            for (const auto &presentQueueFamily : presentQueueFamilies) {
                if (graphicsQueueFamily.queueFamilyIndex != presentQueueFamily.queueFamilyIndex) {
                    candidate.graphicsQueue = graphicsQueueFamily;
                    candidate.presentQueue  = presentQueueFamily;
                    foundSeparate           = true;
                    break;
                }
            }
            if (foundSeparate) {
                break;
            }
        }

        if (!foundSeparate) {
            candidate.graphicsQueue = graphicsQueueFamilies[0];
            candidate.presentQueue  = presentQueueFamilies[0];
        }

        _deviceCandidates.push_back(candidate);
    }
    std::ranges::sort(_deviceCandidates, [](const PhysicalDeviceCandidate &a, const PhysicalDeviceCandidate &b) {
        return a.score > b.score;
    });

    if (_deviceCandidates.empty()) {
        YA_CORE_ERROR("No suitable physical devices found");
        return;
    }

    const auto &selected = _deviceCandidates.front();
    m_PhysicalDevice     = selected.device;
    _graphicsQueueFamily = selected.graphicsQueue;
    _presentQueueFamily  = selected.presentQueue;

    YA_CORE_INFO("Selected physical device: {}", (uintptr_t)m_PhysicalDevice);
    YA_CORE_INFO("Graphics queue idx: {} count: {}", _graphicsQueueFamily.queueFamilyIndex, _graphicsQueueFamily.queueCount);
    YA_CORE_INFO("Present queue idx {} count: {}", _presentQueueFamily.queueFamilyIndex, _presentQueueFamily.queueCount);

    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &_physicalMemoryProperties);
    // YA_CORE_INFO("Memory Type Count: {}", _memoryProperties.memoryTypeCount);
}

void VulkanRender::createSurface()
{
    bool ok = onCreateSurface.executeIfBound(&(*_instance), &_surface);
    YA_CORE_ASSERT(ok, "Failed to create surface!");
}

bool VulkanRender::createLogicDevice(uint32_t graphicsQueueCount, uint32_t presentQueueCount)
{
    if (_deviceCandidates.empty()) {
        findPhysicalDevice();
    }

    if (_deviceCandidates.empty()) {
        YA_CORE_ERROR("No suitable physical devices available for logical device creation");
        return false;
    }

    auto tryCreateForCandidate = [&](const PhysicalDeviceCandidate &candidate) -> VkResult {
        m_PhysicalDevice     = candidate.device;
        _graphicsQueueFamily = candidate.graphicsQueue;
        _presentQueueFamily  = candidate.presentQueue;
        m_LogicalDevice      = VK_NULL_HANDLE;
        _graphicsQueues.clear();
        _presentQueues.clear();
        bOnlyOnePresentQueue = false;

        if ((int)graphicsQueueCount > _graphicsQueueFamily.queueCount)
        {
            YA_CORE_ERROR("Requested graphics queue count {} exceeds available queue count {} for family index {}",
                          graphicsQueueCount,
                          _graphicsQueueFamily.queueCount,
                          _graphicsQueueFamily.queueFamilyIndex);
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        if ((int)presentQueueCount > _presentQueueFamily.queueCount)
        {
            YA_CORE_ERROR("Requested present queue count {} exceeds available queue count {} for family index {}",
                          presentQueueCount,
                          _presentQueueFamily.queueCount,
                          _presentQueueFamily.queueFamilyIndex);
            return VK_ERROR_INITIALIZATION_FAILED;
        }


        bool     bSameQueueFamily   = isGraphicsPresentSameQueueFamily();
        uint32_t combinedQueueCount = graphicsQueueCount + presentQueueCount;
        uint32_t queueCreateCount   = graphicsQueueCount;
        if (bSameQueueFamily)
        {
            if (combinedQueueCount > static_cast<uint32_t>(_graphicsQueueFamily.queueCount)) {
                if (_graphicsQueueFamily.queueCount == 1) {
                    bOnlyOnePresentQueue = true;
                    queueCreateCount     = 1;
                }
                else {
                    YA_CORE_ERROR("Requested combined queue count {} exceeds available queue count {} for family index {}",
                                  combinedQueueCount,
                                  _graphicsQueueFamily.queueCount,
                                  _graphicsQueueFamily.queueFamilyIndex);
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            else {
                queueCreateCount = combinedQueueCount;
            }
        }
        else {
            if ((int)graphicsQueueCount > _graphicsQueueFamily.queueCount) {
                YA_CORE_ERROR("Requested graphics queue count {} exceeds available queue count {} for family index {}",
                              graphicsQueueCount,
                              _graphicsQueueFamily.queueCount,
                              _graphicsQueueFamily.queueFamilyIndex);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if ((int)presentQueueCount > _presentQueueFamily.queueCount) {
                YA_CORE_ERROR("Requested present queue count {} exceeds available queue count {} for family index {}",
                              presentQueueCount,
                              _presentQueueFamily.queueCount,
                              _presentQueueFamily.queueFamilyIndex);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            queueCreateCount = graphicsQueueCount;
        }


        std::vector<float> graphicsQueuePriorities(queueCreateCount, 0.0f);
        std::vector<float> presentQueuePriorities(presentQueueCount, 1.0f);
        if (bSameQueueFamily && !bOnlyOnePresentQueue) {
            graphicsQueuePriorities.insert(graphicsQueuePriorities.end(),
                                           presentQueuePriorities.begin(),
                                           presentQueuePriorities.end());
        }

        std::vector<VkDeviceQueueCreateInfo> deviceQueueCIs;
        deviceQueueCIs.push_back({
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .queueFamilyIndex = static_cast<uint32_t>(_graphicsQueueFamily.queueFamilyIndex),
            .queueCount       = queueCreateCount,
            .pQueuePriorities = graphicsQueuePriorities.data(),
        });
        if (!bSameQueueFamily)
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
        VK_CALL(vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr));
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        VK_CALL(vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, availableExtensions.data()));

        uint32_t layerCount = 0;
        VK_CALL(vkEnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, nullptr));
        std::vector<VkLayerProperties> availableLayers(layerCount);
        VK_CALL(vkEnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, availableLayers.data()));

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
            YA_CORE_ERROR("Vulkan device is not suitable for {}", candidate.properties.deviceName);
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }

        VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
        physicalDeviceFeatures.samplerAnisotropy        = VK_TRUE;
        physicalDeviceFeatures.fillModeNonSolid         = VK_TRUE;

        {
            VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
            dynamicRenderingFeatures.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
            dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &dynamicRenderingFeatures;

            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);
            if (!dynamicRenderingFeatures.dynamicRendering)
            {
                YA_CORE_ERROR("Dynamic rendering is not supported on {}", candidate.properties.deviceName);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
        }

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
            .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            .pNext            = nullptr,
            .dynamicRendering = VK_TRUE,
        };

        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{
            .sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
            .pNext                            = &dynamicRenderingFeatures,
            .extendedDynamicState3PolygonMode = VK_TRUE,
        };

        VkDeviceCreateInfo deviceCreateInfo = {
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext                   = &extendedDynamicState3Features,
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
        VK_CALL(ret);
        if (ret != VK_SUCCESS) {
            return ret;
        }

        for (uint32_t i = 0; i < graphicsQueueCount; i++)
        {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(m_LogicalDevice, _graphicsQueueFamily.queueFamilyIndex, i, &queue);
            if (queue == VK_NULL_HANDLE) {
                YA_CORE_ERROR("Failed to get graphics queue!");
                vkDestroyDevice(m_LogicalDevice, nullptr);
                m_LogicalDevice = VK_NULL_HANDLE;
                return VK_ERROR_INITIALIZATION_FAILED;
            }

            _graphicsQueues.emplace_back(_graphicsQueueFamily.queueFamilyIndex, i, queue, false);
            setDebugObjectName(VK_OBJECT_TYPE_QUEUE,
                               queue,
                               std::format("GraphicsQueue_{}", i).c_str());
        }
        for (uint32_t i = 0; i < presentQueueCount; i++)
        {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(m_LogicalDevice, _presentQueueFamily.queueFamilyIndex, i, &queue);
            if (queue == VK_NULL_HANDLE) {
                YA_CORE_ERROR("Failed to get present queue!");
                vkDestroyDevice(m_LogicalDevice, nullptr);
                m_LogicalDevice = VK_NULL_HANDLE;
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            _presentQueues.emplace_back(_presentQueueFamily.queueFamilyIndex, i, queue, true);
            setDebugObjectName(VK_OBJECT_TYPE_QUEUE,
                               queue,
                               std::format("PresentQueue_{}", i).c_str());
        }

        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &_physicalMemoryProperties);
        return VK_SUCCESS;
    };

    for (const auto &candidate : _deviceCandidates) {
        YA_CORE_INFO("Trying device: {}", candidate.properties.deviceName);
        VkResult ret = tryCreateForCandidate(candidate);
        if (ret == VK_SUCCESS) {
            return true;
        }
        YA_CORE_WARN("Failed to create logical device for {}: {}",
                     candidate.properties.deviceName,
                     ret);
    }

    return false;
}

bool VulkanRender::createCommandPool()

{
    _graphicsCommandPool = std::make_unique<VulkanCommandPool>(
        this,
        &getGraphicsQueues()[0],
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    // _presentCommandPool  = std::move(VulkanCommandPool(this, _presentQueueFamily.queueFamilyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));

    return _graphicsCommandPool->_handle != VK_NULL_HANDLE;
}

void VulkanRender::createPipelineCache()
{
    VkPipelineCacheCreateInfo ci{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .initialDataSize = 0,
        .pInitialData    = nullptr,

    };

    vkCreatePipelineCache(getDevice(), &ci, nullptr, &_pipelineCache);

    // Initialize descriptor helper (using new since we use raw pointer)
    _descriptorHelper = new VulkanDescriptorHelper(this);
}

IDescriptorSetHelper *VulkanRender::getDescriptorHelper()
{
    return _descriptorHelper;
}

void VulkanRender::initExtensionFunctions()
{
#define ASSIGN_VK_FUNCTION(v, func)                              \
    v = (PFN_##func)vkGetDeviceProcAddr(m_LogicalDevice, #func); \
    if (nullptr == (v)) {                                        \
        YA_CORE_WARN(#func "not available");                     \
    }                                                            \
    else {                                                       \
        YA_CORE_INFO(#func "loaded successfully");               \
    }

    // Load VK_EXT_extended_dynamic_state3 extension function pointer
    ASSIGN_VK_FUNCTION(VulkanCommandBuffer::s_vkCmdSetPolygonModeEXT, vkCmdSetPolygonModeEXT);
    // ASSIGN_VK_FUNCTION(VulkanCommandBuffer::s_vkCmdBeginRenderingKHR,  );

    // TODO: low vulkan sdk?
    VulkanCommandBuffer::s_vkCmdBeginRenderingKHR = vkCmdBeginRendering;
    VulkanCommandBuffer::s_vkCmdEndRenderingKHR   = vkCmdEndRendering;
}

void VulkanRender::allocateCommandBuffers(uint32_t size, std::vector<VkCommandBuffer> &outCommandBuffers)
{
    outCommandBuffers.resize(size, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < size; ++i) {
        bool ok = _graphicsCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, outCommandBuffers[i]);
        if (!ok) {
            YA_CORE_ERROR("Failed to allocate command buffer for index {}", i);
            return;
        }
    }
}

// IRender interface implementation
void VulkanRender::allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>> &outBuffers)
{
    std::vector<VkCommandBuffer> vkCommandBuffers;
    allocateCommandBuffers(count, vkCommandBuffers);

    outBuffers.clear();
    outBuffers.reserve(count);
    for (auto vkCmdBuf : vkCommandBuffers) {
        outBuffers.push_back(std::make_shared<VulkanCommandBuffer>(this, vkCmdBuf));
    }
}

bool VulkanRender::isFeatureSupported(
    std::string_view                          contextStr,
    const std::vector<VkExtensionProperties> &availableExtensions,
    const std::vector<VkLayerProperties>     &availableLayers,
    const std::vector<DeviceFeature>         &requestExtensions,
    const std::vector<DeviceFeature>         &requestLayers,
    std::vector<const char *>                &outExtensionNames,
    std::vector<const char *>                &outLayerNames,
    bool                                      bDebug)
{

    if (bDebug) {
        YA_CORE_INFO("=================================");
        YA_CORE_INFO("Available {} layers:", contextStr);
        std::string line = "\n";
        for (size_t i = 0; i < availableLayers.size(); i += 3) {
            for (size_t j = i; j < std::min(i + 3, availableLayers.size()); ++j) {
                line += std::format("{:<35}", availableLayers[j].layerName);
            }
            line.push_back('\n');
        }
        YA_CORE_INFO("{}", line);
        YA_CORE_INFO("Available {} extensions:", contextStr);
        line = "\n";
        for (size_t i = 0; i < availableExtensions.size(); i += 3) {
            for (size_t j = i; j < std::min(i + 3, availableExtensions.size()); ++j) {
                line += std::format("{:<35}|", availableExtensions[j].extensionName);
            }
            line.push_back('\n');
        }
        YA_CORE_INFO("{}", line);
    }


    // Clear the output vectors first
    outExtensionNames.clear();
    outLayerNames.clear();

    for (const auto &feat : requestExtensions) {
        if (std::find(outExtensionNames.begin(), outExtensionNames.end(), feat.name.c_str()) != outExtensionNames.end()) {
            continue;
        }
        if (!std::any_of(availableExtensions.begin(),
                         availableExtensions.end(),
                         [&feat](const VkExtensionProperties &ext) {
                             return std::strcmp(ext.extensionName, feat.name.c_str()) == 0;
                         }))
        {
            YA_CORE_WARN("Extension {} is not supported by the {}", feat.name, contextStr);
            if (feat.bRequired) {
                return false; // If it's a required extension, return false
            }
            continue;
        }
        outExtensionNames.push_back(feat.name.c_str());
    }

    for (const auto &feat : requestLayers) {
        if (std::find(outExtensionNames.begin(), outExtensionNames.end(), feat.name.c_str()) != outExtensionNames.end()) {
            continue;
        }
        if (!std::any_of(
                availableLayers.begin(),
                availableLayers.end(),
                [&feat](const VkLayerProperties &layer) {
                    return std::string(layer.layerName) == feat.name;
                }))
        {

            YA_CORE_WARN("Layer {} is not supported by the {}", feat.name, contextStr);
            if (feat.bRequired) {
                return false; // If it's a required layer, return false
            }
            continue;
        }
        // Debug log to ensure we're adding the correct layer name
        outLayerNames.push_back(feat.name.c_str());
    }

    YA_CORE_INFO("=================================");
    YA_CORE_INFO("Final Extension Names({}):", outExtensionNames.size());
    for (size_t i = 0; i < outExtensionNames.size(); ++i) {
        YA_CORE_INFO("  Final Extension[{}]: {}", i, outExtensionNames[i]);
    }
    YA_CORE_INFO("Final Layer Names({}):", outLayerNames.size());
    for (size_t i = 0; i < outLayerNames.size(); ++i) {
        YA_CORE_INFO("  Final Layer[{}]: {}", i, outLayerNames[i]);
    }


    return true;
}

void VulkanRender::createSyncResources(int32_t swapchainImageSize)
{
    // Create synchronization objects

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0, // No special flags
    };

    VkResult ret = {};
    // NOTICE : 应该创建与swapchain图像数量相同的信号量数量
    // 以避免不同图像之间的资源竞争（如信号量) VUID-vkQueueSubmit-pSignalSemaphores-00067

    // ✅ 为每个飞行帧分配独立的同步对象数组
    // 这是Flight Frames的核心：避免不同帧之间的资源竞争
    imageSubmittedSignalSemaphores.resize(swapchainImageSize); // 渲染完成信号量
    frameImageAvailableSemaphores.resize(flightFrameSize);
    frameFences.resize(flightFrameSize);

    // ✅ 循环创建每个swapchain image的同步对象
    for (uint32_t i = 0; i < (uint32_t)swapchainImageSize; i++) {
        // 创建渲染完成信号量：当GPU完成渲染时发出信号
        ret = vkCreateSemaphore(this->getDevice(), &semaphoreInfo, nullptr, &imageSubmittedSignalSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create render finished semaphore! Result: {}", ret);
        this->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                 imageSubmittedSignalSemaphores[i],
                                 std::format("RenderFinishedSemaphore_{}", i).c_str());
    }

    for (uint32_t i = 0; i < flightFrameSize; i++) {
        // 创建图像可用信号量：当swapchain图像准备好被渲染时发出信号
        ret = vkCreateSemaphore(this->getDevice(), &semaphoreInfo, nullptr, &frameImageAvailableSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create image available semaphore! Result: {}", ret);
        // 创建帧fence：用于CPU等待GPU完成整个帧的处理
        // ⚠️ 重要：初始状态设为已信号(SIGNALED)，这样第一帧不会被阻塞
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT // 开始时就处于已信号状态
        };

        ret = vkCreateFence(this->getDevice(), &fenceInfo, nullptr, &frameFences[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "failed to create fence!");
        this->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                 frameFences[i],
                                 std::format("FrameFence_{}", i).c_str());
        this->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                 frameImageAvailableSemaphores[i],
                                 std::format("ImageAvailableSemaphore_{}", i).c_str());
    }
}

void VulkanRender::releaseSyncResources()
{
    for (uint32_t i = 0; i < flightFrameSize; i++) {
        vkDestroySemaphore(this->getDevice(), frameImageAvailableSemaphores[i], this->getAllocator());
        vkDestroyFence(this->getDevice(), frameFences[i], this->getAllocator());
    }
    for (uint32_t i = 0; i < imageSubmittedSignalSemaphores.size(); i++) {
        vkDestroySemaphore(this->getDevice(), imageSubmittedSignalSemaphores[i], this->getAllocator());
    }
}



const VkAllocationCallbacks *VulkanRender::getAllocator()
{
    // static VkAllocationCallbacks allocator{
    //     .pUserData     = nullptr,
    //     .pfnAllocation = [](void *pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void * {
    //     },
    //     .pfnReallocation = [](void *pUserData, void *pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void * {
    //         if (pOriginal) {
    //             free(pOriginal);
    //         }
    //         return aligned_alloc(alignment, size);
    //     },
    //     .pfnFree          = [](void *pUserData, void *pMemory) {
    //         if (pMemory) {
    //             free(pMemory);
    //         } },
    //     .pfnInternalAlloc = nullptr,
    //     .pfnInternalFree  = nullptr,
    // };
    return VK_NULL_HANDLE;
}



// MARK: Being/End
bool VulkanRender::begin(int32_t *outImageIndex)
{
    YA_PROFILE_FUNCTION()

    // 这确保CPU不会在GPU还在使用资源时(present)就开始修改它们
    // 例如：如果MAX_FRAMES_IN_FLIGHT=2，当渲染第3帧时，等待第1帧完成
    VK_CALL(vkWaitForFences(this->getDevice(),
                            1,
                            &frameFences[currentFrameIdx],
                            VK_TRUE,
                            UINT64_MAX));

    // 重置fence为未信号状态，准备给GPU在本帧结束时发送信号
    VK_CALL(vkResetFences(this->getDevice(), 1, &frameFences[currentFrameIdx]));

    auto vkSwapChain = this->getSwapchain<VulkanSwapChain>();


    uint32_t imageIndex = 0;
    VkResult ret        = vkSwapChain->acquireNextImage(
        frameImageAvailableSemaphores[currentFrameIdx], // 当前帧的图像可用信号量
        frameFences[currentFrameIdx],                   // 等待上一present完成
        imageIndex);

    // Do a sync recreation here, Can it be async?(just return and register a frame task)
    if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(this->getDevice());

        // current ignore the size in ci
        YA_CORE_INFO("Swapchain out of date or suboptimal, recreating...");
        bool ok = vkSwapChain->recreate(vkSwapChain->getCreateInfo());
        if (!ok) {
            YA_CORE_ERROR("Failed to recreate swapchain");
            return false;
        }

        // If swapchain recreation was skipped (e.g., window minimized), return and retry next frame
        if (vkSwapChain->getImageSize() == 0) {
            YA_CORE_WARN("Swapchain has no images (window minimized), skipping frame");
            *outImageIndex = -1;
            return true;
        }

        ret = vkSwapChain->acquireNextImage(frameImageAvailableSemaphores[currentFrameIdx],
                                            frameFences[currentFrameIdx],
                                            imageIndex);

        if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
            YA_CORE_ERROR("Failed to acquire next image: {}", ret);
            return false;
        }
        YA_CORE_ASSERT(imageIndex >= 0 && imageIndex < vkSwapChain->getImageSize(),
                       "Invalid image index: {}. Swapchain image size: {}",
                       imageIndex,
                       vkSwapChain->getImageSize());
    }
    *outImageIndex = static_cast<int32_t>(imageIndex);
    return true;
}

bool VulkanRender::end(int32_t imageIndex, std::vector<void *> cmdBufs)
{
    YA_PROFILE_FUNCTION()
    // If cmdBufs is not empty, use legacy single-pass mode
    if (!cmdBufs.empty()) {
        submitToQueue(
            cmdBufs,
            {frameImageAvailableSemaphores[currentFrameIdx]},
            {imageSubmittedSignalSemaphores[imageIndex]},
            frameFences[currentFrameIdx]);
    }
    // Otherwise, App has already submitted with custom sync

    // Present the image
    int result = presentImage(imageIndex, {imageSubmittedSignalSemaphores[imageIndex]});

    if (result == VK_SUBOPTIMAL_KHR) {
        YA_CORE_INFO("Swapchain suboptimal, recreating...");
        auto vkSwapChain = this->getSwapchain<VulkanSwapChain>();
        VK_CALL(vkDeviceWaitIdle(this->getDevice()));
        bool ok = vkSwapChain->recreate(vkSwapChain->getCreateInfo());
        if (!ok) {
            YA_CORE_ERROR("Failed to recreate swapchain after suboptimal!");
        }
        return false;
    }

    currentFrameIdx = (currentFrameIdx + 1) % flightFrameSize;
    return true;
}

void VulkanRender::submitToQueue(
    const std::vector<void *> &cmdBufs,
    const std::vector<void *> &waitSemaphores,
    const std::vector<void *> &signalSemaphores,
    void                      *fence)
{
    std::vector<VkSemaphore> vkWaitSemaphores;
    std::vector<VkSemaphore> vkSignalSemaphores;

    vkWaitSemaphores.reserve(waitSemaphores.size());
    for (auto sem : waitSemaphores) {
        vkWaitSemaphores.push_back(static_cast<VkSemaphore>(sem));
    }

    vkSignalSemaphores.reserve(signalSemaphores.size());
    for (auto sem : signalSemaphores) {
        vkSignalSemaphores.push_back(static_cast<VkSemaphore>(sem));
    }

    _graphicsQueues[0].submit(
        cmdBufs.data(),
        cmdBufs.size(),
        vkWaitSemaphores,
        vkSignalSemaphores,
        fence ? static_cast<VkFence>(fence) : VK_NULL_HANDLE);
}

int VulkanRender::presentImage(int32_t imageIndex, const std::vector<void *> &waitSemaphores)
{
    std::vector<VkSemaphore> vkWaitSemaphores;
    vkWaitSemaphores.reserve(waitSemaphores.size());
    for (auto sem : waitSemaphores) {
        vkWaitSemaphores.push_back(static_cast<VkSemaphore>(sem));
    }

    auto     vkSwapChain = this->getSwapchain<VulkanSwapChain>();
    VkResult result      = vkSwapChain->presentImage(imageIndex, vkWaitSemaphores);
    return static_cast<int>(result);
}

void *VulkanRender::createSemaphore(const char *debugName)
{
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult    ret       = vkCreateSemaphore(getDevice(), &semaphoreInfo, getAllocator(), &semaphore);
    YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create semaphore! Result: {}", ret);

    if (debugName && bSupportDebugUtils) {
        setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE, semaphore, debugName);
    }

    return semaphore;
}

void VulkanRender::destroySemaphore(void *semaphore)
{
    if (semaphore) {
        vkDestroySemaphore(getDevice(), static_cast<VkSemaphore>(semaphore), getAllocator());
    }
}

int32_t VulkanRender::getMemoryIndex(VkMemoryPropertyFlags properties, uint32_t memoryTypeBits) const
{
    if (_physicalMemoryProperties.memoryTypeCount == 0)
    {
        YA_CORE_ERROR("Physical device has no memory types!");
        return -1;
    }

    for (uint32_t i = 0; i < _physicalMemoryProperties.memoryTypeCount; ++i) {
        if ((memoryTypeBits & (1 << i)) && // Check if the bit is set
            (_physicalMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return static_cast<int32_t>(i);
        }
    }

    YA_CORE_ERROR("No suitable memory type found for properties: {} and memoryTypeBits: {}", (uint32_t)properties, memoryTypeBits);
    return -1;
}

void VulkanRender::initWindow(const RenderCreateInfo &ci)
{
#if USE_SDL
    _windowProvider = new SDLWindowProvider();
    _windowProvider->init();
    _windowProvider->recreate(WindowCreateInfo{
        .renderAPI = ci.renderAPI,
        .width     = ci.swapchainCI.width,
        .height    = ci.swapchainCI.height,
    });

    auto sdlWindow = static_cast<SDLWindowProvider *>(_windowProvider);
    YA_CORE_ASSERT(sdlWindow, "SDLWindowProvider is not initialized correctly");

    onCreateSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
        return sdlWindow->onCreateVkSurface(instance, surface);
    });
    onReleaseSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
        sdlWindow->onDestroyVkSurface(instance, surface);
    });
    onGetRequiredInstanceExtensions.set([sdlWindow]() {
        std::vector<DeviceFeature> extensions;
        for (const char *ext : sdlWindow->onGetVkInstanceExtensions()) {
            extensions.push_back({
                .name      = ext,
                .bRequired = true,
            });
        }
        return extensions;
    });
#endif
}
} // namespace ya