
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

    static stdptr<VulkanImageView> create(VulkanRender *render, stdptr<VulkanImage> image, VkImageAspectFlags aspectFlags);

  private:
    bool init(VulkanRender *render, stdptr<VulkanImage> image, VkImageAspectFlags aspectFlags);
};
} // namespace ya