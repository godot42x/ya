#include "VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"

VulkanImage::~VulkanImage()
{
    if (bOwned) {
        VK_FREE(Memory, _render->getLogicalDevice(), _imageMemory);
        VK_DESTROY(Image, _render->getLogicalDevice(), _handle);
    }
}

void VulkanImage::transfer(VkCommandBuffer cmdBuf, VulkanBuffer *srcBuffer, VulkanImage *dstImage)
{

    VkBufferImageCopy copyRegion{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = VkImageSubresourceLayers{
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .mipLevel       = 0,
             .baseArrayLayer = 0,
             .layerCount     = 1,
        },
        .imageOffset = VkOffset3D{0, 0, 0},
        .imageExtent = VkExtent3D{
            .width  = dstImage->getWidth(),
            .height = dstImage->getHeight(),
            .depth  = 1,
        },
    };
    vkCmdCopyBufferToImage(cmdBuf,
                           srcBuffer->getHandle(),
                           dstImage->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copyRegion);
}

bool VulkanImage::transitionLayout(VkCommandBuffer cmdBuf, const VulkanImage *image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout)
{
    if (image == VK_NULL_HANDLE) {
        YA_CORE_ERROR("VulkanImage::transitionImageLayout image is null");
        return false;
    }
    if (newLayout == oldLayout) {
        return true;
    }

    VkImageMemoryBarrier imb{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        // TODO
        .srcAccessMask       = {},
        .dstAccessMask       = {},
        .oldLayout           = oldLayout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image->getHandle(),
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1,
        },

    };

    VkPipelineStageFlags srcStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    // Source layouts (old)
    // The source access mask controls actions to be finished on the old
    // layout before it will be transitioned to the new layout.
    switch (oldLayout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        // Image layout is undefined (or does not matter).
        // Only valid as initial layout. No flags required.
        imb.srcAccessMask = 0;
        break;

    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        // Image is preinitialized.
        // Only valid as initial layout for linear images; preserves memory
        // contents. Make sure host writes have finished.
        imb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image is a color attachment.
        // Make sure writes to the color buffer have finished
        imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image is a depth/stencil attachment.
        // Make sure any writes to the depth/stencil buffer have finished.
        imb.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image is a transfer source.
        // Make sure any reads from the image have finished
        imb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image is a transfer destination.
        // Make sure any writes to the image have finished.
        imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image is read by a shader.
        // Make sure any shader reads from the image have finished
        imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        YA_ERROR("Unsupported layout transition: {}->{}", std::to_string(oldLayout), std::to_string(newLayout));
        UNREACHABLE();
        return false;
    }

    // Target layouts (new)
    // The destination access mask controls the dependency for the new image
    // layout.
    switch (newLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image will be used as a transfer destination.
        // Make sure any writes to the image have finished.
        imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image will be used as a transfer source.
        // Make sure any reads from and writes to the image have finished.
        imb.srcAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image will be used as a color attachment.
        // Make sure any writes to the color buffer have finished.
        imb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image layout will be used as a depth/stencil attachment.
        // Make sure any writes to depth/stencil buffer have finished.
        imb.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image will be read in a shader (sampler, input attachment).
        // Make sure any writes to the image have finished.
        if (imb.srcAccessMask == 0)
        {
            imb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        YA_ERROR("Unsupported layout transition: {}->{}", std::to_string(oldLayout), std::to_string(newLayout));
        UNREACHABLE();
        return false;
    }


    vkCmdPipelineBarrier(cmdBuf,
                         srcStage,
                         destStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &imb);

    return true;
}


bool VulkanImage::allocate()
{
    _format     = toVk(_ci.format);
    _usageFlags = toVk(_ci.usage);

    VkImageCreateInfo imageCreateInfo{
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext     = nullptr,
        .flags     = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = _format,
        .extent    = {
               .width  = _ci.extent.width,
               .height = _ci.extent.height,
               .depth  = _ci.extent.depth,
        },
        .mipLevels             = _ci.mipLevels,
        .arrayLayers           = 1,
        .samples               = toVk(_ci.samples),
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = _usageFlags,
        .sharingMode           = toVk(_ci.sharingMode),
        .queueFamilyIndexCount = _ci.queueFamilyIndexCount,
        .pQueueFamilyIndices   = _ci.pQueueFamilyIndices,
        .initialLayout         = toVk(_ci.initialLayout),
    };

    VK_CALL(vkCreateImage(_render->getLogicalDevice(), &imageCreateInfo, nullptr, &_handle));


    // allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(_render->getLogicalDevice(), _handle, &memRequirements);
    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = static_cast<uint32_t>(_render->getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memRequirements.memoryTypeBits)),
    };

    VK_CALL(vkAllocateMemory(_render->getLogicalDevice(), &allocInfo, nullptr, &_imageMemory));
    VK_CALL(vkBindImageMemory(_render->getLogicalDevice(), _handle, _imageMemory, 0));

    bOwned = true;
    return true;
}
