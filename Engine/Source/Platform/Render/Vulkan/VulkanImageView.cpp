#include "VulkanImageView.h"
#include "VulkanImage.h"
#include "VulkanRender.h"

VulkanImageView::VulkanImageView(VulkanRender *render, const VulkanImage *image, VkImageAspectFlags aspectFlags)
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

    VK_CALL(vkCreateImageView(_render->getDevice(), &ci, _render->getAllocator(), &_handle));
}

VulkanImageView::~VulkanImageView()
{
    // Note: image view is destroyed along with the image
    VK_DESTROY(ImageView, _render->getDevice(), _handle);
}
