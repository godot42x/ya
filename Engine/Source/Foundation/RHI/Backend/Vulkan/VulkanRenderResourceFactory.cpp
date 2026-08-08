#include "VulkanRenderResourceFactory.h"

#include "Foundation/RHI/Backend/Vulkan/VulkanBuffer.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanImage.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanImageView.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanRender.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanSampler.h"

namespace ya
{

std::shared_ptr<IBuffer> VulkanRenderResourceFactory::createBuffer(const BufferCreateInfo& desc)
{
    return VulkanBuffer::create(_render, desc);
}

std::shared_ptr<Sampler> VulkanRenderResourceFactory::createSampler(const SamplerDesc& desc)
{
    return makeShared<VulkanSampler>(_render, desc);
}

std::shared_ptr<IImage> VulkanRenderResourceFactory::createImage(const ImageCreateInfo& desc)
{
    return VulkanImage::create(_render, desc);
}

std::shared_ptr<IImage> VulkanRenderResourceFactory::importImage(const ImportedImageDesc& desc)
{
    YA_CORE_ASSERT(desc.nativeHandle, "Cannot import an image with a null native handle");
    YA_CORE_ASSERT(desc.ownership == EImportedImageOwnership::BorrowedNative,
                   "Owned native image import is not implemented");

    auto image = VulkanImage::from(
        _render,
        static_cast<VkImage>(desc.nativeHandle),
        toVk(desc.format),
        toVk(desc.usage),
        desc.extent.width,
        desc.extent.height,
        desc.mipLevels,
        desc.arrayLayers,
        desc.initialLayout);
    if (!desc.label.empty()) {
        image->setDebugName(desc.label);
    }
    return image;
}

std::shared_ptr<IImageView> VulkanRenderResourceFactory::createImageView(
    std::shared_ptr<IImage> image,
    const ImageViewCreateInfo& desc)
{
    auto vkImage = std::dynamic_pointer_cast<VulkanImage>(std::move(image));
    YA_CORE_ASSERT(vkImage, "Cannot create a Vulkan image view from a non-Vulkan image");

    VulkanImageView::CreateInfo createInfo{};
    createInfo.viewType       = toVk(desc.viewType);
    createInfo.aspectFlags    = desc.aspectFlags;
    createInfo.baseMipLevel   = desc.baseMipLevel;
    createInfo.levelCount     = desc.levelCount;
    createInfo.baseArrayLayer = desc.baseArrayLayer;
    createInfo.layerCount     = desc.layerCount;
    if (!desc.components.isIdentity()) {
        createInfo.components = toVk(desc.components);
    }

    auto view = VulkanImageView::create(_render, std::move(vkImage), createInfo);
    if (view && !desc.label.empty()) {
        view->setDebugName(desc.label);
    }
    return view;
}

} // namespace ya
