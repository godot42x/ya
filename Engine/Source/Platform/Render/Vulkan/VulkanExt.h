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

    PFN_vkCmdBeginDebugUtilsLabelEXT    pfnCmdBeginDebugUtilsLabelEXT    = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT      pfnCmdEndDebugUtilsLabelEXT      = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT   pfnCmdInsertDebugUtilsLabelEXT   = nullptr;
    PFN_vkQueueBeginDebugUtilsLabelEXT  pfnQueueBeginDebugUtilsLabelEXT  = nullptr;
    PFN_vkQueueEndDebugUtilsLabelEXT    pfnQueueEndDebugUtilsLabelEXT    = nullptr;
    PFN_vkQueueInsertDebugUtilsLabelEXT pfnQueueInsertDebugUtilsLabelEXT = nullptr;


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

    // wait command buffer execute
    void cmdBeginLabel(CommandBufferHandle cmdBuf, const char *labelName, const glm::vec4 *color = nullptr)
    {
        VkDebugUtilsLabelEXT label = {
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = labelName,
        };
        if (color) {
            std::memcpy(label.color, glm::value_ptr(*color), sizeof(label.color));
        }
        // vkCmdBeginDebugUtilsLabelEXT(cmdBuf.as<VkCommandBuffer>(), &label);
        pfnCmdBeginDebugUtilsLabelEXT(cmdBuf.as<VkCommandBuffer>(), &label);
    }
    void cmdEndLabel(CommandBufferHandle cmdBuf)
    {
        // vkCmdEndDebugUtilsLabelEXT(cmdBuf.as<VkCommandBuffer>());
        pfnCmdEndDebugUtilsLabelEXT(cmdBuf.as<VkCommandBuffer>());
    }

    // directly set label for queue
    void queueBeginLabel(VkQueue queue, const char *labelName, const glm::vec4 *color = nullptr)
    {
        VkDebugUtilsLabelEXT label = {
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = labelName,
        };
        if (color) {
            std::memcpy(label.color, glm::value_ptr(*color), sizeof(label.color));
        }
        // vkQueueBeginDebugUtilsLabelEXT(queue, &label);
        pfnQueueBeginDebugUtilsLabelEXT(queue, &label);
    }
    void queueEndLabel(VkQueue queue)
    {
        // vkQueueEndDebugUtilsLabelEXT(queue);
        pfnQueueEndDebugUtilsLabelEXT(queue);
    }
};

} // namespace ya