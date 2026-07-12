#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/Texture.h"

namespace ya
{

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

    // Determine view type based on image properties
    uint32_t        layerCount = vkImage->getArrayLayers();
    uint32_t        mipLevels  = vkImage->getMipLevels();
    EImageViewType::T viewType = EImageViewType::View2D;
    if (layerCount == 6 && (vkImage->_ci.flags & EImageCreateFlag::CubeCompatible)) {
        viewType = EImageViewType::ViewCube;
    }
    else if (layerCount > 1) {
        viewType = EImageViewType::View2DArray;
    }

    ImageViewCreateInfo viewCI{
        .label          = label,
        .viewType       = viewType,
        .aspectFlags    = static_cast<EImageAspect::T>(aspect),
        .baseMipLevel   = 0,
        .levelCount     = mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = layerCount,
    };
    auto imageView = render->getResourceFactory()->createImageView(image, viewCI);
    if (!imageView) {
        YA_CORE_ERROR("Failed to create image view for external image: {}", label);
        return nullptr;
    }

    // Create Texture using wrap factory method (FrameBuffer owns the Texture wrapper)
    auto texture     = Texture::wrap(vkImage, imageView, label);
    texture->_width  = _width;
    texture->_height = _height;
    texture->_format = vkImage->getFormat();

    return texture;
}

bool VulkanFrameBuffer::onRecreate(const FrameBufferCreateInfo& ci)
{
    clean();
    _width         = ci.width;
    _height        = ci.height;
    _maxLayerCount = 1;

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
        _maxLayerCount = std::max(_maxLayerCount, ci.colorImages[i]->getArrayLayers());
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
        _maxLayerCount = std::max(_maxLayerCount, ci.depthImages->getArrayLayers());
    }

    _resolveTexture.reset();
    if (ci.resolveImage) {
        std::string label = std::format("{}_Resolve", ci.label);
        _resolveTexture   = createTexture(
            ci.resolveImage,
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
    if (_resolveTexture && _resolveTexture->getImageView()) {
        vkImageViews.push_back(_resolveTexture->getImageView()->getHandle().as<VkImageView>());
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
        .layers          = _maxLayerCount,

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
