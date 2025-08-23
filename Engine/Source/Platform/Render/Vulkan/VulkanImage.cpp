#include "VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "VulkanRender.h"

VulkanImage::~VulkanImage()
{
    if (bOwned) {
        VK_FREE(Memory, _render->getLogicalDevice(), imageMemory);
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

    VK_CALL(vkAllocateMemory(_render->getLogicalDevice(), &allocInfo, nullptr, &imageMemory));
    VK_CALL(vkBindImageMemory(_render->getLogicalDevice(), _handle, imageMemory, 0));

    bOwned = true;
    return true;
}

VulkanImageView::VulkanImageView(VulkanRender *render, std::shared_ptr<VulkanImage> image, VkImageAspectFlags aspectFlags)
{
    _render = render;
    VkImageViewCreateInfo ci{
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = image->getHandle(),
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = image->getFormat(),
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

    VK_CALL(vkCreateImageView(_render->getLogicalDevice(), &ci, _render->getAllocator(), &_handle));
}

VulkanImageView::~VulkanImageView()
{
    // Note: image view is destroyed along with the image
    VK_DESTROY(ImageView, _render->getLogicalDevice(), _handle);
}
