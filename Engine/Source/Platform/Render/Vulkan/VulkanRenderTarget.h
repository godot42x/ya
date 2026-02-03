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
    // Configuration
    EFormat::T      _colorFormat       = EFormat::R8G8B8A8_UNORM;
    EFormat::T      _depthFormat       = EFormat::Undefined;
    ESampleCount::T _samples           = ESampleCount::Sample_1;
    bool            _bSwapChainTarget  = false;
    uint32_t        _frameBufferCount  = 1;
    uint32_t        _currentFrameIndex = 0;

    // Resources - per frame (for swapchain targets or multi-buffering)
    struct FrameAttachments
    {
        std::shared_ptr<VulkanImage>     colorImage;
        std::shared_ptr<VulkanImageView> colorImageView;
        std::shared_ptr<VulkanImage>     depthImage;
        std::shared_ptr<VulkanImageView> depthImageView;

        // For legacy RenderPass API: VkFramebuffer
        std::shared_ptr<VulkanFrameBuffer> frameBuffer;

        // For swapchain targets: we wrap external images, don't own them
        bool ownsColorImage = true;
    };
    std::vector<FrameAttachments> _frameAttachments;

    // Render reference
    VulkanRender *_vkRender   = nullptr;
    IRenderPass  *_renderPass = nullptr; // For legacy RenderPass API (not owned)

  public:

    VulkanRenderTarget(const VulkanRenderTarget &)            = delete;
    VulkanRenderTarget &operator=(const VulkanRenderTarget &) = delete;
    VulkanRenderTarget(VulkanRenderTarget &&)                 = delete;
    VulkanRenderTarget &operator=(VulkanRenderTarget &&)      = delete;

    explicit VulkanRenderTarget(const RenderTargetCreateInfo &ci);
    virtual ~VulkanRenderTarget() override;

    // ===== IRenderTarget interface implementation =====
    void init() override;
    void recreate() override;
    void destroy() override;
    void beginFrame() override;

    [[nodiscard]] IImage     *getColorImage(uint32_t index = 0) const override;
    [[nodiscard]] IImageView *getColorImageView(uint32_t index = 0) const override;
    [[nodiscard]] IImage     *getDepthImage() const override;
    [[nodiscard]] IImageView *getDepthImageView() const override;

    [[nodiscard]] uint32_t   getColorAttachmentCount() const override { return 1; } // Currently single color attachment
    [[nodiscard]] bool       hasDepthAttachment() const override { return _depthFormat != EFormat::Undefined; }
    [[nodiscard]] EFormat::T getColorFormat() const override { return _colorFormat; }
    [[nodiscard]] EFormat::T getDepthFormat() const override { return _depthFormat; }
    [[nodiscard]] bool       isSwapChainTarget() const override { return _bSwapChainTarget; }
    [[nodiscard]] uint32_t   getCurrentFrameIndex() const override { return _currentFrameIndex; }
    [[nodiscard]] uint32_t   getFrameBufferCount() const override { return _frameBufferCount; }

    // ===== Legacy RenderPass API Support =====
    [[nodiscard]] IFrameBuffer *getFrameBuffer() const override;
    [[nodiscard]] bool          hasFrameBuffer() const override { return _renderPass != nullptr; }

    void onRenderGUI() override;

  private:
    void createAttachments();
    void destroyAttachments();
    void createFrameBuffers(); // Create VkFramebuffers for legacy RenderPass API
    void destroyFrameBuffers();
};

} // namespace ya
