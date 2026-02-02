
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
    const VulkanImage *_image       = nullptr;
    VkImageView        _handle      = VK_NULL_HANDLE;
    VkFormat           _format      = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags _aspectFlags = 0;

    VulkanImageView(VulkanRender *render, const VulkanImage *image, VkImageAspectFlags aspectFlags);
    virtual ~VulkanImageView();

    // IImageView interface
    ImageViewHandle getHandle() const override { return ImageViewHandle{_handle}; }
    const IImage   *getImage() const override;

    // Vulkan-specific accessor
    VkImageView getVkImageView() const { return _handle; }

    EFormat::T getFormat() const override
    {
        return EFormat::fromVk(_format);
    }

    void setDebugName(const std::string &name);
};
} // namespace ya