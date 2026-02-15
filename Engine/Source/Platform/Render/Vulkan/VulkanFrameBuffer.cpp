#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Render/Core/Texture.h"

namespace ya
{

// std::shared_ptr<Texture> VulkanFrameBuffer::createAttachmentTexture(
//     const FrameBufferAttachmentInfo& attachInfo,
//     const std::string&               label)
// {
//     // Convert FrameBufferAttachmentInfo to ImageCreateInfo
//     ya::ImageCreateInfo imageCI{
//         .label         = label,
//         .format        = attachInfo.format,
//         .extent        = {.width = _width, .height = _height, .depth = 1},
//         .mipLevels     = 1,
//         .samples       = attachInfo.msaaSamples.has_value()
//                            ? static_cast<ESampleCount::T>(attachInfo.msaaSamples.value())
//                            : ESampleCount::Sample_1,
//         .usage         = attachInfo.usage,
//         .initialLayout = EImageLayout::Undefined,
//     };

//     // Create VulkanImage
//     auto vkImage = VulkanImage::create(render, imageCI);
//     if (!vkImage) {
//         YA_CORE_ERROR("Failed to create image for framebuffer attachment: {}", label);
//         return nullptr;
//     }

//     // Create VulkanImageView
//     VkImageAspectFlags aspect = attachInfo.isDepth
//                                   ? VK_IMAGE_ASPECT_DEPTH_BIT
//                                   : VK_IMAGE_ASPECT_COLOR_BIT;

//     auto vkImageView = VulkanImageView::create(render, vkImage, aspect);
//     if (!vkImageView) {
//         YA_CORE_ERROR("Failed to create image view for framebuffer attachment: {}", label);
//         return nullptr;
//     }

//     // Create Texture using wrap factory method (FrameBuffer owns the Texture)
//     auto texture     = Texture::wrap(vkImage, vkImageView, label);
//     texture->_width  = _width;
//     texture->_height = _height;
//     texture->_format = attachInfo.format;

//     return texture;
// }

std::shared_ptr<Texture> VulkanFrameBuffer::createTexture(
    const stdptr<IImage>& image,
    const std::string&    label,
    VkImageAspectFlags    aspect)
{
    auto vkImage = std::static_pointer_cast<VulkanImage>(image);
    if (!vkImage) {
        YA_CORE_ERROR("Failed to cast external image to VulkanImage: {}", label);
        return nullptr;
    }

    // Create VulkanImageView for the external image
    auto vkImageView = VulkanImageView::create(render, vkImage, aspect);
    if (!vkImageView) {
        YA_CORE_ERROR("Failed to create image view for external image: {}", label);
        return nullptr;
    }

    // Create Texture using wrap factory method (FrameBuffer owns the Texture wrapper)
    auto texture     = Texture::wrap(vkImage, vkImageView, label);
    texture->_width  = _width;
    texture->_height = _height;
    texture->_format = vkImage->getFormat();

    return texture;
}

bool VulkanFrameBuffer::onRecreate(const FrameBufferCreateInfo& ci)
{
    clean();
    _width  = ci.width;
    _height = ci.height;

    // Determine which mode to use
    // bool useExternalImages = !ci.colorImages.empty();

    // if (useExternalImages) {
    // Mode 2: Wrap external images into Textures (swapchain scenario)
    _colorTextures.clear();
    _colorTextures.reserve(ci.colorImages.size());

    for (size_t i = 0; i < ci.colorImages.size(); ++i) {
        std::string label   = std::format("{}_Color{}", ci.label, i);
        auto        texture = createTexture(
            ci.colorImages[i],
            label,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (!texture) {
            YA_CORE_ERROR("Failed to wrap external color image {} for framebuffer: {}", i, ci.label);
            return false;
        }
        _colorTextures.push_back(std::move(texture));
    }

    // Depth attachment from external if provided
    _depthTexture.reset();
    if (ci.depthImages) {
        std::string label = std::format("{}_Depth", ci.label);
        _depthTexture     = createTexture(
            ci.depthImages,
            label,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!_depthTexture) {
            YA_CORE_ERROR("Failed to wrap external depth image for framebuffer: {}", ci.label);
            return false;
        }
    }

    _resolveTexture.reset();
    if (ci.resolveImages) {
        std::string label = std::format("{}_Resolve", ci.label);
        _resolveTexture   = createTexture(
            ci.resolveImages,
            label,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (!_resolveTexture) {
            YA_CORE_ERROR("Failed to wrap external resolve image for framebuffer: {}", ci.label);
            return false;
        }
    }


    // }
    // else {
    //     // Mode 1: Create textures from specs
    //     _colorTextures.clear();
    //     _colorTextures.reserve(ci.colorAttachments.size());

    //     for (size_t i = 0; i < ci.colorAttachments.size(); ++i) {
    //         std::string label   = std::format("{}_Color{}", ci.label, i);
    //         auto        texture = createAttachmentTexture(ci.colorAttachments[i], label);
    //         if (!texture) {
    //             YA_CORE_ERROR("Failed to create color attachment {} for framebuffer: {}", i, ci.label);
    //             return false;
    //         }
    //         _colorTextures.push_back(std::move(texture));
    //     }

    //     // Create depth attachment texture if specified
    //     _depthTexture.reset();
    //     if (ci.depthAttachment.has_value()) {
    //         std::string label = std::format("{}_Depth", ci.label);
    //         _depthTexture     = createAttachmentTexture(ci.depthAttachment.value(), label);
    //         if (!_depthTexture) {
    //             YA_CORE_ERROR("Failed to create depth attachment for framebuffer: {}", ci.label);
    //             return false;
    //         }
    //     }
    // }

    // if no render pass, just return (dynamic rendering mode)
    if (!ci.renderPass) {
        return true;
    }

    // Create the Vulkan framebuffer for renderpass API
    std::vector<VkImageView> vkImageViews;
    vkImageViews.reserve(_colorTextures.size() + 1);

    for (auto& tex : _colorTextures) {
        if (tex && tex->imageView) {
            vkImageViews.push_back(tex->imageView->getHandle().as<VkImageView>());
        }
    }
    if (_depthTexture && _depthTexture->imageView) {
        vkImageViews.push_back(_depthTexture->imageView->getHandle().as<VkImageView>());
    }

    VkFramebufferCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .renderPass      = ci.renderPass->getHandleAs<VkRenderPass>(),
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
    YA_CORE_TRACE("Created framebuffer: {}, {} with {} attachments", ci.label, (uintptr_t)_framebuffer, vkImageViews.size());

    return true;
}

void VulkanFrameBuffer::clean()
{
    VK_DESTROY(Framebuffer, render->getDevice(), _framebuffer);
    clearAttachments(); // Clears _colorTextures and _depthTexture
}

} // namespace ya