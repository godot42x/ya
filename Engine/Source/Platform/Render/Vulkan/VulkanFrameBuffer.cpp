#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"

namespace ya
{


bool VulkanFrameBuffer::onRecreate(const FrameBufferCreateInfo &ci)
{
    clean();
    _width  = ci.width;
    _height = ci.height;

    _colorImageViews.clear();
    _colorImageViews.reserve(ci.colorImages.size());
    for (auto &image : ci.colorImages)
    {
        _colorImageViews.push_back(
            VulkanImageView::create(render,
                                    std::static_pointer_cast<VulkanImage>(image),
                                    VK_IMAGE_ASPECT_COLOR_BIT));
    }

    _depthImageView.reset();
    if (ci.depthImage)
    {
        _depthImageView = VulkanImageView::create(render,
                                                  std::static_pointer_cast<VulkanImage>(ci.depthImage),
                                                  VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    // _stencilImageView.reset();
    // if (ci.stencilImage)
    // {
    //     _stencilImageView = VulkanImageView::create(render,
    //                                                 std::static_pointer_cast<VulkanImage>(ci.stencilImage),
    //                                                 VK_IMAGE_ASPECT_STENCIL_BIT);
    // }


    // if no render pass, just return
    if (!ci.renderPass) {
        return true;
    }

    // or crate the vulkan framebuffer for renderpass api

    std::vector<VkImageView> vkImageViews;
    vkImageViews.reserve(_colorImageViews.size() + 1 + 1);
    for (auto &imageView : _colorImageViews)
    {
        vkImageViews.push_back(imageView->getHandle().as<VkImageView>());
    }
    if (_depthImageView)
    {
        vkImageViews.push_back(_depthImageView->getHandle().as<VkImageView>());
    }



    VkFramebufferCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .renderPass      = _renderPass->getHandleAs<VkRenderPass>(),
        .attachmentCount = static_cast<uint32_t>(vkImageViews.size()),
        .pAttachments    = vkImageViews.data(),
        .width           = _width,
        .height          = _height,
        .layers          = 1,

    };
    VkResult result = vkCreateFramebuffer(render->getDevice(),
                                          &createInfo,
                                          nullptr,
                                          &_framebuffer);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Failed to create framebuffer: {}", result);
        return false;
    }
    YA_CORE_TRACE("Created framebuffer: {}, {} with {} attachments", _label,  (uintptr_t)_framebuffer, vkImageViews.size());

    return true;
}


void VulkanFrameBuffer::clean()
{
    VK_DESTROY(Framebuffer, render->getDevice(), _framebuffer);
    _colorImageViews.clear();
    _depthImageView.reset();
    // _stencilImageView.reset();

    _colorImages.clear();
    _depthImage.reset();
    // _stencilImage.reset();
}

} // namespace ya
