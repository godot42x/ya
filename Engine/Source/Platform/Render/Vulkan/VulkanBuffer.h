#pragma once

#include <vulkan/vulkan.h>


struct VulkanBuffer
{
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;

    // Optional: for staging buffers
    VkBufferUsageFlags    usageFlags       = 0;
    VkMemoryPropertyFlags memoryProperties = 0;


    VulkanBuffer(uint32_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryProperties)
        : size(size), usageFlags(usageFlags), memoryProperties(memoryProperties)
    {

        VkBufferCreateInfo ci{
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = size,
            .usage       = usageFlags,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    }
};