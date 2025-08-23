#include "VulkanUtils.h"
#include "Core/Log.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"



#if USE_SDL_IMG
    #include <SDL3_image/SDL_image.h>
#elif USE_STB_IMG
    #include <stb_image.h>
#endif


bool VulkanUtils::hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

// Static method implementations
uint32_t VulkanUtils::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if (typeFilter & (1 << i) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    YA_CORE_ASSERT(false, "failed to find suitable memory type!");
    return -1;
}

bool VulkanUtils::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory)
{
    VkBufferCreateInfo bufferInfo{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
    {
        YA_CORE_ASSERT(false, "failed to create buffer");
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device, outBuffer, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, properties),
    };

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outBufferMemory) != VK_SUCCESS)
    {
        YA_CORE_ASSERT(false, "failed to allocate buffer memory!");
    }
    vkBindBufferMemory(device, outBuffer, outBufferMemory, 0);

    return true;
}

void VulkanUtils::createImage(VkDevice device, VkPhysicalDevice physicalDevice,
                              uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageBits, VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory)
{
    VkImageCreateInfo imageInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = 0,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = {width, height, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = tiling,
        .usage         = usageBits,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        YA_CORE_ASSERT(false, "failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties),
    };

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        YA_CORE_ASSERT(false, "failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

VkImageView VulkanUtils::createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo createInfo{
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = image,
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask     = aspectFlags,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    VkImageView imageView = VK_NULL_HANDLE;
    VkResult    result    = vkCreateImageView(device, &createInfo, nullptr, &imageView);
    YA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create image view!");

    return imageView;
}

VkCommandBuffer VulkanUtils::beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanUtils::endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &commandBuffer,
    };

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanUtils::transitionImageLayout(VulkanCommandPool *pool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    UNIMPLEMENTED();
    // VkCommandBuffer commandBuffer = pool->beginSingleTimeCommands();

    // VkImageMemoryBarrier barrier{
    //     .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //     .oldLayout           = oldLayout,
    //     .newLayout           = newLayout,
    //     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    //     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    //     .image               = image,
    //     .subresourceRange    = {
    //            .baseMipLevel   = 0,
    //            .levelCount     = 1,
    //            .baseArrayLayer = 0,
    //            .layerCount     = 1,
    //     },
    // };

    // VkPipelineStageFlags sourceStage      = 0;
    // VkPipelineStageFlags destinationStage = 0;

    // if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    //     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    //     if (hasStencilComponent(format)) {
    //         barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    //     }
    // }
    // else {
    //     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // }

    // if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    //     barrier.srcAccessMask = 0;
    //     barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //     sourceStage           = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    //     destinationStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
    // }
    // else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    //     barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //     barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    //     sourceStage           = VK_PIPELINE_STAGE_TRANSFER_BIT;
    //     destinationStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    // }
    // else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    //     barrier.srcAccessMask = 0;
    //     barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    //     sourceStage           = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    //     destinationStage      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // }
    // // 可以添加更多转换案例，例如：
    // else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    //     barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    //     barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //     sourceStage           = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    //     destinationStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
    // }
    // else {
    //     YA_CORE_ASSERT(false, "unsupported layout transition!");
    // }

    // vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // pool->endSingleTimeCommands(commandBuffer);
}

void VulkanUtils::copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{

    VkCommandBuffer   commandBuffer = beginSingleTimeCommands(device, commandPool);
    VkBufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .mipLevel       = 0,
             .baseArrayLayer = 0,
             .layerCount     = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(device, commandPool, graphicsQueue, commandBuffer);
}

VkFormat VulkanUtils::findSupportedImageFormat(VkPhysicalDevice             physicalDevice,
                                               const std::vector<VkFormat> &candidates,
                                               VkImageTiling                tiling,
                                               VkFormatFeatureFlags         features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    YA_CORE_ASSERT(false, "failed to find supported format!");
    return VK_FORMAT_UNDEFINED;
}

void VulkanUtils::createTextureImage(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue,
                                     const char *path, VkImage &outImage, VkDeviceMemory &outImageMemory)
{

    UNIMPLEMENTED();
    //     int   texWidth, texHeight, texChannels;
    //     void *pixels;

    // #if USE_SDL_IMG
    //     // SDL implementation
    // #else
    //     pixels = stbi_load(path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    //     YA_CORE_ASSERT(pixels, "failed to load texture image! {}", path);
    // #endif

    //     VkDeviceSize imageSize = texWidth * texHeight * 4;

    //     VkBuffer       stagingBuffer;
    //     VkDeviceMemory stagingBufferMemory;

    //     if (!VulkanUtils::createBuffer(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory)) {
    //         YA_CORE_ERROR("Failed to create staging buffer");
    //         return;
    //     }

    //     void *data;
    //     vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    //     std::memcpy(data, pixels, static_cast<size_t>(imageSize));
    //     vkUnmapMemory(device, stagingBufferMemory);

    // #if USE_SDL_IMG
    //     // SDL cleanup
    // #else
    //     stbi_image_free(pixels);
    // #endif

    //     createImage(device,
    //                 physicalDevice,
    //                 texWidth,
    //                 texHeight,
    //                 VK_FORMAT_R8G8B8A8_UNORM,
    //                 VK_IMAGE_TILING_OPTIMAL,
    //                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    //                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    //                 outImage,
    //                 outImageMemory);

    //     transitionImageLayout(device,
    //                           commandPool,
    //                           graphicsQueue,
    //                           outImage,
    //                           VK_FORMAT_R8G8B8A8_UNORM,
    //                           VK_IMAGE_LAYOUT_UNDEFINED,
    //                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    //     copyBufferToImage(device,
    //                       commandPool,
    //                       graphicsQueue,
    //                       stagingBuffer,
    //                       outImage,
    //                       static_cast<uint32_t>(texWidth),
    //                       static_cast<uint32_t>(texHeight));

    //     transitionImageLayout(device,
    //                           commandPool,
    //                           graphicsQueue,
    //                           outImage,
    //                           VK_FORMAT_R8G8B8A8_UNORM,
    //                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    //     vkDestroyBuffer(device, stagingBuffer, nullptr);
    //     vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VulkanUtils::copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                             VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = size,
    };
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(device, commandPool, graphicsQueue, commandBuffer);
}
