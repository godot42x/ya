#pragma once

#include "Foundation/Core/Base.h"

#include "Foundation/RHI/Backend/Vulkan/VulkanUtils.h"
#include "reflects-core/enum.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace ya
{


struct VulkanRender;

struct VulkanDebugUtils
{
    struct ObjectKey
    {
        VkObjectType objectType    = VK_OBJECT_TYPE_UNKNOWN;
        uint64_t     objectHandle  = 0;

        bool operator==(const ObjectKey& other) const
        {
            return objectType == other.objectType && objectHandle == other.objectHandle;
        }
    };

    struct ObjectKeyHash
    {
        size_t operator()(const ObjectKey& key) const
        {
            const size_t typeHash   = std::hash<int>{}(static_cast<int>(key.objectType));
            const size_t handleHash = std::hash<uint64_t>{}(key.objectHandle);
            return typeHash ^ (handleHash + 0x9e3779b97f4a7c15ull + (typeHash << 6) + (typeHash >> 2));
        }
    };

    // Function pointers for debug extensions
    PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateDebugUtilsMessengerEXT  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT    pfnSetDebugUtilsObjectNameEXT    = nullptr;


    VkDebugUtilsMessengerEXT _debugUtilsMessenger = nullptr;

    VulkanRender *_renderer = nullptr;

    mutable std::mutex                                                _metadataMutex;
    std::unordered_map<ObjectKey, std::string, ObjectKeyHash>         _objectNames;
    std::unordered_map<ObjectKey, std::string, ObjectKeyHash>         _objectSummaries;

    VulkanDebugUtils() = delete;
    VulkanDebugUtils(VulkanRender *renderer)
    {
        _renderer = renderer;
    }

    // report is old version api!
    // PFN_vkCreateDebugReportCallbackEXT  pfnCreateDebugReportCallbackEXT  = nullptr;
    // PFN_vkDestroyDebugReportCallbackEXT pfnDestroyDebugReportCallbackEXT = nullptr;

    void initInstanceLevel();
    void initDeviceLevel();
    void rewriteDebugUtils();

    void destroy();

    const VkDebugUtilsMessengerCreateInfoEXT &getDebugMessengerCreateInfoExt();

    void setObjectName(VkObjectType objectType, uint64_t objectHandle, const char *name);
    void setObjectSummary(VkObjectType objectType, uint64_t objectHandle, std::string summary);
    [[nodiscard]] std::string getObjectName(VkObjectType objectType, uint64_t objectHandle) const;
    [[nodiscard]] std::string getObjectSummary(VkObjectType objectType, uint64_t objectHandle) const;
};

} // namespace ya
