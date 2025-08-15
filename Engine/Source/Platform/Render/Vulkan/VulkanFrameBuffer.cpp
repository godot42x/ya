#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"



bool VulkanFrameBuffer::recreate(std::vector<std::shared_ptr<VulkanImage>> images, uint32_t width, uint32_t height)
{
    clean();
    this->width  = width;
    this->height = height;


    _images = images;
    _imageViews.resize(images.size());
    for (int i = 0; i < images.size(); i++)
    {
        bool bDepth    = VulkanUtils::isDepthOnlyFormat(_images[i]->getFormat());
        _imageViews[i] = VulkanUtils::createImageView(
            render->getLogicalDevice(),
            _images[i]->getHandle(),
            _images[i]->getFormat(),
            bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
    }

    VkFramebufferCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = renderPass->getHandle(),
        .attachmentCount = static_cast<uint32_t>(_imageViews.size()),
        .pAttachments    = _imageViews.data(),
        .width           = width,
        .height          = height,
        .layers          = 1,

    };
    VkResult result = vkCreateFramebuffer(render->getLogicalDevice(), &createInfo, nullptr, &_framebuffer);
    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to create framebuffer: {}", result);
        return false;
    }
    NE_CORE_INFO("Created framebuffer: {} with {} attachments", (uintptr_t)_framebuffer, _imageViews.size());

    return true;
}

void VulkanFrameBuffer::clean()
{
    VK_DESTROY(Framebuffer, render->getLogicalDevice(), _framebuffer);
    for (auto &imageView : _imageViews)
    {
        VK_DESTROY(ImageView, render->getLogicalDevice(), imageView);
    }
    for (auto &image : _images)
    {
        image.reset(); // Reset shared pointers to release resources
    }
}
