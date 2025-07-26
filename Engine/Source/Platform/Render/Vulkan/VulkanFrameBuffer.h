
#pragma once

#include "Platform/Render/Vulkan/VulkanRender.h"
#include <vulkan/vulkan.h>


struct VulkanFrameBuffer
{
    VulkanRender     *render;
    VulkanRenderPass *renderPass;
    uint32_t          width;
    uint32_t          height;

    std::vector<VkImageView> _imageViews;
    VkFramebuffer            _framebuffer = VK_NULL_HANDLE;

    VulkanFrameBuffer() = default;
    VulkanFrameBuffer(VulkanRender *render, VulkanRenderPass *renderPass,
                      uint32_t width, uint32_t height)
    {
        this->render     = render;
        this->renderPass = renderPass;
        this->width      = width;
        this->height     = height;
    }
    ~VulkanFrameBuffer() {}

    bool recreate(std::vector<VkImage> images, uint32_t width, uint32_t height);

    void clean();

    VkFramebuffer getHandle() const { return _framebuffer; }
};