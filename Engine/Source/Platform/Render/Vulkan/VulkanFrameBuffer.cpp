#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"

namespace ya
{


bool VulkanFrameBuffer::recreate(std::vector<std::shared_ptr<IImage>> images, uint32_t width, uint32_t height)
{
    std::vector<std::shared_ptr<VulkanImage>> vkImages;
    for (auto &img : images)
    {
        vkImages.push_back(std::dynamic_pointer_cast<VulkanImage>(img));
    }
    return recreateImpl(vkImages, width, height);
}
bool VulkanFrameBuffer::recreateImpl(std::vector<std::shared_ptr<VulkanImage>> images, uint32_t width, uint32_t height)
{
    clean();
    this->width  = width;
    this->height = height;


    _images = images;
    _imageViews.resize(images.size());
    for (int i = 0; i < images.size(); i++)
    {
        bool bDepth    = VulkanUtils::isDepthStencilFormat(_images[i]->getVkFormat());
        _imageViews[i] = makeShared<VulkanImageView>(render, _images[i].get(), bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
    }

    std::vector<VkImageView> vkImageViews;
    vkImageViews.reserve(_imageViews.size());
    for (auto &iv : _imageViews)
    {
        vkImageViews.push_back(iv->getVkImageView());
    }

    VkFramebufferCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .renderPass      = renderPass->getVkHandle(),
        .attachmentCount = static_cast<uint32_t>(vkImageViews.size()),
        .pAttachments    = vkImageViews.data(),
        .width           = width,
        .height          = height,
        .layers          = 1,

    };
    VkResult result = vkCreateFramebuffer(render->getDevice(), &createInfo, nullptr, &_framebuffer);
    if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Failed to create framebuffer: {}", result);
        return false;
    }
    YA_CORE_TRACE("Created framebuffer: {} with {} attachments", (uintptr_t)_framebuffer, _imageViews.size());

    return true;
}


void VulkanFrameBuffer::clean()
{
    VK_DESTROY(Framebuffer, render->getDevice(), _framebuffer);
    _imageViews.clear();
    _images.clear();
    // for (auto &imageView : _imageViews)
    // {
    //     imageView.reset(); // Reset shared pointers to release resources
    // }
    // for (auto &image : _images)
    // {
    //     image.reset(); // Reset shared pointers to release resources
    // }
}

} // namespace ya
