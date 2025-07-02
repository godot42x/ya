#pragma once

#include <Core/Log.h>
#include <vulkan/vulkan.h>

struct VulkanUtils
{

    static bool hasStencilComponent(VkFormat format);

    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter, VkMemoryPropertyFlags properties);

    static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                             VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    static void createImage(VkDevice device, VkPhysicalDevice physicalDevice,
                            uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageBits,
                            VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory);

    static VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    static void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                      VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    static void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                  VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    static void            endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

    static void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                           VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    static VkFormat findSupportedImageFormat(VkPhysicalDevice physicalDevice, 
                                             const std::vector<VkFormat> &candidates,
                                             VkImageTiling tiling, 
                                             VkFormatFeatureFlags features);

    static void createTextureImage(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue,
                                   const char *path, VkImage &outImage, VkDeviceMemory &outImageMemory);
};