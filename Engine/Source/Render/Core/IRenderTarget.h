#pragma once

#include "Core/Delegate.h"
#include "Render/RenderDefines.h"
#include <memory>
#include <string>

namespace ya
{

// Forward declarations
struct IImage;
struct IImageView;
struct ICommandBuffer;
struct IFrameBuffer;
struct IRenderPass;

/**
 * @brief Frame context containing per-frame camera data
 */
struct FrameContext
{
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 cameraPos  = glm::vec3(0.0f);
};

/**
 * @brief Configuration for creating a RenderTarget
 * Simplified: only needs format and extent, no RenderPass dependency
 */
struct RenderTargetCreateInfo
{
    std::string     label            = "RenderTarget";
    EFormat::T      colorFormat      = EFormat::R8G8B8A8_UNORM;
    EFormat::T      depthFormat      = EFormat::Undefined; // Undefined = no depth
    Extent2D        extent           = {800, 600};
    bool            bSwapChainTarget = false; // If true, use swapchain images instead of creating our own
    uint32_t        frameBufferCount = 1;     // For swapchain targets, this is ignored (uses swapchain count)
    ESampleCount::T samples          = ESampleCount::Sample_1;

    // === Legacy RenderPass API support ===
    // If renderPass is provided, creates VkFramebuffer for traditional rendering pipeline
    // If nullptr, only manages Images/ImageViews for Dynamic Rendering
    IRenderPass *renderPass = nullptr;
};

/**
 * @brief Abstract interface for render targets
 *
 * A RenderTarget represents a set of attachments (color + optional depth) that can be
 * rendered to. It manages the underlying Image and ImageView resources.
 *
 * Key design principles:
 * - No dependency on RenderPass (supports both traditional and dynamic rendering)
 * - No dependency on Swapchain (extent/format provided at creation)
 * - MaterialSystems are managed externally for flexibility
 * - ClearValues are provided through beginRendering API externally
 * - Dirty flag triggers recreation on next begin()
 */
struct IRenderTarget
{
    std::string       label          = "None";
    Extent2D          _extent        = {0, 0};
    bool              bDirty         = false;
    ERenderingMode::T _renderingMode = ERenderingMode::None;

    /**
     * @brief Triggered when attachments are recreated (e.g., resize, format change)
     * Listeners should invalidate any cached ImageView references
     */
    MulticastDelegate<void()> onAttachmentsRecreated;

    IRenderTarget()          = default;
    virtual ~IRenderTarget() = default;

    // Delete copy operations
    IRenderTarget(const IRenderTarget &)            = delete;
    IRenderTarget &operator=(const IRenderTarget &) = delete;

    // Default move operations
    IRenderTarget(IRenderTarget &&)            = default;
    IRenderTarget &operator=(IRenderTarget &&) = default;

    /**
     * @brief Initialize the render target (create initial resources)
     */
    virtual void init() = 0;

    /**
     * @brief Recreate attachments (called internally when dirty, or externally if needed)
     */
    virtual void recreate() = 0;

    /**
     * @brief Destroy render target resources
     */
    virtual void destroy() = 0;

    /**
     * @brief Begin frame - check dirty flag and recreate if needed
     * Call this at the start of each frame before rendering
     */
    virtual void beginFrame() = 0;

    /**
     * @brief Set new extent, marks as dirty
     */
    void setExtent(Extent2D extent)
    {
        if (_extent.width != extent.width || _extent.height != extent.height)
        {
            _extent = extent;
            bDirty  = true;
        }
    }

    [[nodiscard]] const Extent2D &getExtent() const { return _extent; }

    // ===== Attachment Access =====

    /**
     * @brief Get the color image (for use as sampler input, etc.)
     * @param index Attachment index (for multiple color attachments)
     */
    [[nodiscard]] virtual IImage *getColorImage(uint32_t index = 0) const = 0;

    /**
     * @brief Get the color image view (for descriptor binding, etc.)
     * @param index Attachment index
     */
    [[nodiscard]] virtual IImageView *getColorImageView(uint32_t index = 0) const = 0;

    /**
     * @brief Get the depth image (nullptr if no depth attachment)
     */
    [[nodiscard]] virtual IImage *getDepthImage() const = 0;

    /**
     * @brief Get the depth image view (nullptr if no depth attachment)
     */
    [[nodiscard]] virtual IImageView *getDepthImageView() const = 0;

    /**
     * @brief Get number of color attachments
     */
    [[nodiscard]] virtual uint32_t getColorAttachmentCount() const = 0;

    /**
     * @brief Check if this render target has a depth attachment
     */
    [[nodiscard]] virtual bool hasDepthAttachment() const = 0;

    /**
     * @brief Get the color format
     */
    [[nodiscard]] virtual EFormat::T getColorFormat() const = 0;

    /**
     * @brief Get the depth format (Undefined if no depth)
     */
    [[nodiscard]] virtual EFormat::T getDepthFormat() const = 0;

    /**
     * @brief Check if this is a swapchain target
     */
    [[nodiscard]] virtual bool isSwapChainTarget() const = 0;

    /**
     * @brief Get the current frame index (relevant for swapchain targets with multiple images)
     */
    [[nodiscard]] virtual uint32_t getCurrentFrameIndex() const = 0;

    /**
     * @brief Get frame buffer count
     */
    [[nodiscard]] virtual uint32_t getFrameBufferCount() const = 0;

    // ===== Legacy RenderPass API Support =====

    /**
     * @brief Get the VkFramebuffer for traditional RenderPass API
     * @return FrameBuffer pointer, or nullptr if using Dynamic Rendering mode
     */
    [[nodiscard]] virtual IFrameBuffer *getFrameBuffer() const { return nullptr; }

    /**
     * @brief Check if this render target supports traditional RenderPass API
     * @return true if VkFramebuffer is available
     */
    [[nodiscard]] virtual bool hasFrameBuffer() const { return false; }

    /**
     * @brief Render debug GUI for this render target
     */
    virtual void onRenderGUI() {}

    ERenderingMode::T getRenderingMode() const { return _renderingMode; }
};

/**
 * @brief Factory function to create a platform-specific render target
 */
extern std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetCreateInfo &ci);

} // namespace ya
