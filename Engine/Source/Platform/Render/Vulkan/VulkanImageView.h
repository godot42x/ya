
#pragma once

#include "Core/Base.h"

#include <vulkan/vulkan.h>

#include "Render/Core/Image.h"
#include "VulkanUtils.h"

namespace ya
{
struct VulkanRender;
struct VulkanImage;


struct VulkanImageView : public IImageView
{
    VulkanRender      *_render      = nullptr;
    VkImageView        _handle      = VK_NULL_HANDLE;
    VkFormat           _format      = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags _aspectFlags = 0;

    virtual ~VulkanImageView();

    // IImageView interface
    ImageViewHandle getHandle() const override { return ImageViewHandle{_handle}; }

    // Vulkan-specific accessor
    VkImageView getVkImageView() const { return _handle; }

    EFormat::T getFormat() const override
    {
        return EFormat::fromVk(_format);
    }

    void setDebugName(const std::string &name) override;

    struct CreateInfo
    {
        VkImageViewType    viewType       = VK_IMAGE_VIEW_TYPE_2D;
        VkImageAspectFlags aspectFlags    = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t           baseMipLevel   = 0;
        uint32_t           levelCount     = 1;
        uint32_t           baseArrayLayer = 0;
        uint32_t           layerCount     = 1;
    };

    static stdptr<VulkanImageView> create(VulkanRender *render, stdptr<VulkanImage> image, VkImageAspectFlags aspectFlags);
    static stdptr<VulkanImageView> create(VulkanRender *render, stdptr<VulkanImage> image, const CreateInfo &ci);

  private:
    bool init(VulkanRender *render, stdptr<VulkanImage> image, const CreateInfo &ci);
};
} // namespace ya