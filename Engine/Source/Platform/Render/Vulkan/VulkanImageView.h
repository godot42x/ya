
#pragma once

#include "Core/Base.h"

#include <vulkan/vulkan.h>

#include "Render/Render.h"

struct VulkanRender;
struct VulkanImage;


struct VulkanImageView
{
    VulkanRender *_render = nullptr;
    VkImageView   _handle = VK_NULL_HANDLE;

    VulkanImageView(VulkanRender *render, const VulkanImage *image, VkImageAspectFlags aspectFlags);
    virtual ~VulkanImageView();
};