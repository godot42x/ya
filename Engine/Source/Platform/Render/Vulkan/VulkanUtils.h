#pragma once

#include <Core/Log.h>

#include <vulkan/vulkan.h>




struct VulkanUtils
{
    // should rely on vulkan state
    VkDevice         _device;         // logical device
    VkPhysicalDevice _physicalDevice; // physical device
    VkCommandPool    _commandPool;    // command pool for the logical device
    VkQueue          _graphicsQueue;  // graphics queue for the logical device
    VkQueue          _presentQueue;   // present queue for the logical device

    void onRecreateSwapchain(struct VulkanState *state);

    static bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }


    using VkMemoryRequirementTypeBits = uint32_t;
    uint32_t findMemoryType(VkMemoryRequirementTypeBits typeFilter, VkMemoryPropertyFlags properties);


    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);


    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usageBits, VkMemoryPropertyFlags properties,
                     VkImage &image, VkDeviceMemory &imageMemory);

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    void createTextureImage(const char *path, VkImage &outImage, VkDeviceMemory &outImageMemory);

    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);


    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);


    VkCommandBuffer beginSingleTimeCommands();
    void            endSingleTimeCommands(VkCommandBuffer &commandBuffer);


    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
};