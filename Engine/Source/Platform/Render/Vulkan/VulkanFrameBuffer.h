
#pragma once

#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/FrameBuffer.h"
#include "VulkanImage.h"
#include <vulkan/vulkan.h>

namespace ya
{

struct VulkanFrameBuffer : public IFrameBuffer
{
    // TODO: replace render with logicalDevice if without other sub access
    VulkanRender* render{};
    uint32_t      _width{};
    uint32_t      _height{};

    // renderpass api optional
    VkFramebuffer _framebuffer = VK_NULL_HANDLE;

    VulkanFrameBuffer(VulkanRender* inRender) { render = inRender; }
    virtual ~VulkanFrameBuffer()
    {
        clean();
    }

    bool begin(ICommandBuffer* commandBuffer) override { return true; }
    bool end(ICommandBuffer* commandBuffer) override { return true; }

    bool onRecreate(const FrameBufferCreateInfo& ci) override;
    void clean();

    // IFrameBuffer interface
    [[nodiscard]] Extent2D getExtent() const override { return {.width = _width, .height = _height}; }

    // Vulkan-specific accessor
    [[nodiscard]] void*         getHandle() const override { return static_cast<void*>(_framebuffer); }
    [[nodiscard]] VkFramebuffer getVkHandle() const { return _framebuffer; }

  private:

    std::shared_ptr<Texture> createTexture(const stdptr<IImage>& externalImage,
                                           const std::string&    label,
                                               VkImageAspectFlags    aspect);

    // std::shared_ptr<Texture> createAttachmentTexture(const FrameBufferAttachmentInfo &attachInfo,
    //                                                  const std::string               &label);
};

} // namespace ya