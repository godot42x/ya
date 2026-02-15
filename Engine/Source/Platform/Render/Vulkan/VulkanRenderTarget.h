#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/RenderDefines.h"
#include <memory>
#include <vector>

namespace ya
{

// Forward declarations
struct VulkanRender;
struct VulkanImage;
struct VulkanImageView;
struct VulkanFrameBuffer;
struct ISwapchain;
struct IRenderPass;

/**
 * @brief Vulkan-specific implementation of render target
 *
 * Manages color and depth attachments as VulkanImage/VulkanImageView.
 * Supports both standalone rendering and swapchain targets.
 *
 * Supports two rendering modes:
 * 1. Dynamic Rendering (VK_KHR_dynamic_rendering) - no VkFramebuffer needed
 * 2. Traditional RenderPass API - creates VkFramebuffer when renderPass is provided
 *
 * Usage:
 * 1. Create with RenderTargetCreateInfo (optionally with renderPass for legacy mode)
 * 2. Call beginFrame() each frame (handles dirty recreation)
 * 3. Use getColorImageView()/getDepthImageView() for Dynamic Rendering
 * 4. Or use getFrameBuffer() with RenderPass for traditional rendering
 */
struct VulkanRenderTarget : public IRenderTarget
{
  private:


    VulkanRender *_vkRender = nullptr;

    // For RenderPass API
    IRenderPass *_renderPass   = nullptr; // For RenderPass API (not owned)
    uint32_t     _subpassIndex = 0;

    uint32_t _frameBufferCount = 0;


  public:

    VulkanRenderTarget(const VulkanRenderTarget&)            = delete;
    VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
    VulkanRenderTarget(VulkanRenderTarget&&)                 = delete;
    VulkanRenderTarget& operator=(VulkanRenderTarget&&)      = delete;

    VulkanRenderTarget() = default;
    virtual ~VulkanRenderTarget() override;

    // ===== IRenderTarget interface implementation =====
    bool onInit(const RenderTargetCreateInfo& ci) override;
    void recreate() override;
    void destroy() override;

    void beginFrame(ICommandBuffer *cmdBuf) override;
    void endFrame(ICommandBuffer *cmdBuf) override;

    void onRenderGUI() override;

    IFrameBuffer* getFrameBuffer(int32_t index = -1) const;

  private:

    bool recreateImagesAndFrameBuffer(uint32_t frameBufferCount);

    void getAttachmentsFromRenderPass(const IRenderPass* pass, uint32_t subPassIndex);
};

} // namespace ya
