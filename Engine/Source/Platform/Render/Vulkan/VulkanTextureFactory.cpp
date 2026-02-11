#include "VulkanTextureFactory.h"
#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanRender.h"
#include "VulkanUtils.h"

namespace ya
{

// ====== Low-level IImage/IImageView API Implementation ======

IRender *VulkanTextureFactory::getRender() const
{
    return reinterpret_cast<IRender *>(_render);
}

std::shared_ptr<IImage> VulkanTextureFactory::createImage(const ImageCreateInfo &ci)
{
    return VulkanImage::create(_render, ci);
}

std::shared_ptr<IImage> VulkanTextureFactory::createImageFromHandle(void *platformImage, EFormat::T format, EImageUsage::T usage)
{
    VkImage           vkImage = static_cast<VkImage>(platformImage);
    VkImageUsageFlags vkUsage = toVk(usage);
    VkFormat          vkFormat = toVk(format);

    return VulkanImage::from(_render, vkImage, vkFormat, vkUsage);
}

std::shared_ptr<IImageView> VulkanTextureFactory::createImageView(std::shared_ptr<IImage> image, uint32_t aspectFlags)
{
    auto vkImage = std::dynamic_pointer_cast<VulkanImage>(image);
    YA_CORE_ASSERT(vkImage, "Cannot create image view from non-Vulkan image");

    return VulkanImageView::create(_render, vkImage, aspectFlags);
}

std::shared_ptr<IImageView> VulkanTextureFactory::createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo &ci)
{
    auto vkImage = std::dynamic_pointer_cast<VulkanImage>(image);
    YA_CORE_ASSERT(vkImage, "Cannot create image view from non-Vulkan image");

    // Convert RHI ImageViewCreateInfo to Vulkan CreateInfo
    VulkanImageView::CreateInfo vkCi{};
    vkCi.viewType       = static_cast<VkImageViewType>(ci.viewType);
    vkCi.aspectFlags    = ci.aspectFlags;
    vkCi.baseMipLevel   = ci.baseMipLevel;
    vkCi.levelCount     = ci.levelCount;
    vkCi.baseArrayLayer = ci.baseArrayLayer;
    vkCi.layerCount     = ci.layerCount;

    return VulkanImageView::create(_render, vkImage, vkCi);
}

std::shared_ptr<IImageView> VulkanTextureFactory::createCubeMapImageView(
    std::shared_ptr<IImage> image,
    uint32_t                aspectFlags,
    uint32_t                baseMipLevel,
    uint32_t                levelCount,
    uint32_t                baseArrayLayer,
    uint32_t                layerCount)
{
    auto vkImage = std::dynamic_pointer_cast<VulkanImage>(image);
    YA_CORE_ASSERT(vkImage, "Cannot create cubemap image view from non-Vulkan image");

    VulkanImageView::CreateInfo ci{};
    ci.viewType       = VK_IMAGE_VIEW_TYPE_CUBE;
    ci.aspectFlags    = aspectFlags;
    ci.baseMipLevel   = baseMipLevel;
    ci.levelCount     = levelCount;
    ci.baseArrayLayer = baseArrayLayer;
    ci.layerCount     = layerCount;

    return VulkanImageView::create(_render, vkImage, ci);
}

} // namespace ya
