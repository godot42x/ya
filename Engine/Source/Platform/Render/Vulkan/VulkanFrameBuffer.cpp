#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Render/Core/RenderResourceFactory.h"

namespace ya
{

std::shared_ptr<RenderImage> VulkanFrameBuffer::createAttachmentResource(
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

    auto attachment         = std::make_shared<RenderImage>();
    attachment->image       = vkImage;
    attachment->defaultView = std::move(imageView);
    return attachment;
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
    _colorAttachments.clear();
    _colorAttachments.reserve(ci.colorImages.size());

    for (size_t i = 0; i < ci.colorImages.size(); ++i) {
        std::string label   = std::format("{}_Color{}", ci.label, i);
        auto        attachment = createAttachmentResource(
            ci.colorImages[i],
            label,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (!attachment) {
            YA_CORE_ERROR("Failed to wrap external color image {} for framebuffer: {}", i, ci.label);
            return false;
        }
        _colorAttachments.push_back(std::move(attachment));
        _maxLayerCount = std::max(_maxLayerCount, ci.colorImages[i]->getArrayLayers());
    }

    // Depth attachment from external if provided
    _depthAttachment.reset();
    if (ci.depthImages) {
        std::string label = std::format("{}_Depth", ci.label);
        _depthAttachment  = createAttachmentResource(
            ci.depthImages,
            label,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!_depthAttachment) {
            YA_CORE_ERROR("Failed to wrap external depth image for framebuffer: {}", ci.label);
            return false;
        }
        _maxLayerCount = std::max(_maxLayerCount, ci.depthImages->getArrayLayers());
    }

    _resolveAttachment.reset();
    if (ci.resolveImage) {
        std::string label = std::format("{}_Resolve", ci.label);
        _resolveAttachment = createAttachmentResource(
            ci.resolveImage,
            label,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (!_resolveAttachment) {
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
    vkImageViews.reserve(_colorAttachments.size() + 1);

    for (const auto& attachment : _colorAttachments) {
        if (attachment && attachment->getImageView()) {
            vkImageViews.push_back(attachment->getImageView()->getHandle().as<VkImageView>());
        }
    }
    if (_depthAttachment && _depthAttachment->getImageView()) {
        vkImageViews.push_back(_depthAttachment->getImageView()->getHandle().as<VkImageView>());
    }
    if (_resolveAttachment && _resolveAttachment->getImageView()) {
        vkImageViews.push_back(_resolveAttachment->getImageView()->getHandle().as<VkImageView>());
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
