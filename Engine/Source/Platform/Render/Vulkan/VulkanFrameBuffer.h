
#pragma once

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "VulkanImage.h"
#include <vulkan/vulkan.h>


struct VulkanFrameBuffer 
{
    // TODO: replace render with logicalDevice if without other sub access
    VulkanRender     *render{};
    VulkanRenderPass *renderPass{};
    uint32_t          width{};
    uint32_t          height{};

    std::vector<std::shared_ptr<VulkanImage>> _images;
    std::vector<VkImageView>                  _imageViews;

    VkFramebuffer _framebuffer = VK_NULL_HANDLE;

    VulkanFrameBuffer() = default;
    VulkanFrameBuffer(VulkanRender *render, VulkanRenderPass *renderPass,
                      uint32_t width, uint32_t height)
    {
        this->render     = render;
        this->renderPass = renderPass;
        this->width      = width;
        this->height     = height;
    }
    virtual ~VulkanFrameBuffer()
    {
        clean();
    }

    bool recreate(std::vector<std::shared_ptr<VulkanImage>> images, uint32_t width, uint32_t height);

    void clean();

    [[nodiscard]] VkFramebuffer getHandle() const { return _framebuffer; }
    [[nodiscard]] uint32_t      getWidth() const { return width; }
    [[nodiscard]] uint32_t      getHeight() const { return height; }
};