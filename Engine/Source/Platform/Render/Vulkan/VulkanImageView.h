
#pragma once

#include "Core/Base.h"

#include <vulkan/vulkan.h>

#include "Render/Core/Image.h"

namespace ya {
struct VulkanRender;
struct VulkanImage;


struct VulkanImageView : public IImageView
{
    VulkanRender *_render = nullptr;
    VkImageView   _handle = VK_NULL_HANDLE;

    VulkanImageView(VulkanRender *render, const VulkanImage *image, VkImageAspectFlags aspectFlags);
    virtual ~VulkanImageView();

    // IImageView interface
    ImageViewHandle getHandle() const override { return ImageViewHandle{_handle}; }

    // Vulkan-specific accessor
    VkImageView getVkImageView() const { return _handle; }
};
}