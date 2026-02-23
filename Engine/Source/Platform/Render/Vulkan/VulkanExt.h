#pragma once

#include "Core/Base.h"

#include "Platform/Render/Vulkan/VulkanUtils.h"
#include "reflect.cc/enum"
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace ya
{


struct VulkanRender;

struct VulkanDebugUtils
{
    // Function pointers for debug extensions
    PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateDebugUtilsMessengerEXT  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT    pfnSetDebugUtilsObjectNameEXT    = nullptr;


    VkDebugUtilsMessengerEXT _debugUtilsMessenger = nullptr;

    VulkanRender *_renderer = nullptr;

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
};

} // namespace ya