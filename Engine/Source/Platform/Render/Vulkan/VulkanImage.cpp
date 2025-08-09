#include "VulkanImage.h"
#include "VulkanRender.h"

VulkanImage::~VulkanImage()
{
    if (bOwned) {
        VK_FREE(Memory, _render->getLogicalDevice(), imageMemory);
        VK_DESTROY(Image, _render->getLogicalDevice(), _handle);
    }
}


bool VulkanImage::allocate()
{
    _format     = toVk(_ci->format);
    _usageFlags = toVk(_ci->usage);

    VkImageCreateInfo imageCreateInfo{
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext     = nullptr,
        .flags     = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = _format,
        .extent    = {
               .width  = _ci->extent.width,
               .height = _ci->extent.height,
               .depth  = _ci->extent.depth,
        },
        .mipLevels             = _ci->mipLevels,
        .samples               = toVk(_ci->samples),
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = _usageFlags,
        .sharingMode           = toVk(_ci->sharingMode),
        .queueFamilyIndexCount = _ci->queueFamilyIndexCount,
        .pQueueFamilyIndices   = _ci->pQueueFamilyIndices,
        .initialLayout         = toVk(_ci->initialLayout),
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
