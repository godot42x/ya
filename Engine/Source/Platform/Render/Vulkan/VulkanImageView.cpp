#include "VulkanImageView.h"
#include "VulkanImage.h"
#include "VulkanRender.h"
namespace ya
{

bool VulkanImageView::init(VulkanRender *render, stdptr<VulkanImage> image, VkImageAspectFlags aspectFlags)
{
    _render      = render;
    _image       = image;
    _format      = image->getVkFormat();
    _aspectFlags = aspectFlags;
    VkImageViewCreateInfo ci{
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext      = nullptr,
        .flags      = 0,
        .image      = image->getVkImage(),
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = _format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask     = _aspectFlags,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    VK_CALL_RET(vkCreateImageView(_render->getDevice(),
                                  &ci,
                                  _render->getAllocator(),
                                  &_handle));
}

VulkanImageView::~VulkanImageView()
{
    // Note: image view is destroyed along with the image
    VK_DESTROY(ImageView, _render->getDevice(), _handle);
}


void VulkanImageView::setDebugName(const std::string &name)
{
    _render->setDebugObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (void *)_handle, name);
}

stdptr<VulkanImageView> VulkanImageView::create(VulkanRender *render, stdptr<VulkanImage> image, VkImageAspectFlags aspectFlags)
{
    auto ret = makeShared<VulkanImageView>();
    if (!ret->init(render, image, aspectFlags)) {
        return nullptr;
    }
    return ret;
}



} // namespace ya