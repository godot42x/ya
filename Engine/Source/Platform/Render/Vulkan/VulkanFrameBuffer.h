
#pragma once

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/FrameBuffer.h"
#include "VulkanImage.h"
#include <vulkan/vulkan.h>

namespace ya
{

struct VulkanFrameBuffer : public IFrameBuffer
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

    bool recreate(std::vector<std::shared_ptr<IImage>> images, uint32_t width, uint32_t height) override;
    bool recreateImpl(std::vector<std::shared_ptr<VulkanImage>> images, uint32_t width, uint32_t height);

    void clean();

    // IFrameBuffer interface
    [[nodiscard]] void    *getHandle() const override { return static_cast<void *>(_framebuffer); }
    [[nodiscard]] Extent2D getExtent() const override { return {width, height}; }

    // Vulkan-specific accessor
    [[nodiscard]] VkFramebuffer getVkHandle() const { return _framebuffer; }
};

} // namespace ya