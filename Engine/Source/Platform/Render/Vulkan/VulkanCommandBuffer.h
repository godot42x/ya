
#pragma once
#include "Core/Base.h"
#include "Core/Log.h"
#include "VulkanUtils.h"


#include <vulkan/vulkan.h>


struct VulkanRender;


struct VulkanCommandPool : disable_copy
{
    VkCommandPool _handle = VK_NULL_HANDLE;
    VulkanRender *_render = nullptr;

    VulkanCommandPool(VulkanRender *render, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);

    bool allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer &outCommandBuffer);

    void cleanup();
};


inline void begin(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags flags = 0)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = flags,
        .pInheritanceInfo = nullptr,
    };
    VK_CALL(vkBeginCommandBuffer(commandBuffer, &beginInfo));
}

inline void end(VkCommandBuffer commandBuffer)
{
    VK_CALL(vkEndCommandBuffer(commandBuffer));
}