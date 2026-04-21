#pragma once

#include "Core/Delegate.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/RenderDefines.h"
#include <memory>
#include <string>

namespace ya
{

// Forward declarations
struct IImage;
struct IImageView;
struct ICommandBuffer;
struct IRenderPass;



/**
 * @brief Configuration for creating a RenderTarget
 * Supports multiple color attachments and both RenderPass/Dynamic Rendering modes
 */
struct RenderTargetCreateInfo
{
    std::string       label                         = "RenderTarget";
    ERenderingMode::T renderingMode                 = ERenderingMode::DynamicRendering;
    bool              bSwapChainTarget              = false; // If true, use swapchain images instead of creating our own
    int32_t           swapChianColorAttachmentIndex = 0;

    Extent2D extent           = {.width = 800, .height = 600};
    uint32_t frameBufferCount = 1; // for custom render targets
    uint32_t layerCount       = 1; // for array textures or cubemaps

    [[deprecated("Unimplemented")]] uint32_t mipLevels = 1; // Number of mip levels for attachments

    struct AttachmentSpec
    {
        std::vector<AttachmentDescription>   colorAttach   = {}; // Support multiple color attachments
        std::optional<AttachmentDescription> depthAttach   = {}; // Undefined = no depth
        std::optional<AttachmentDescription> resolveAttach = {}; // Optional resolve attachment
    };

    struct RenderPassSpec
    {
        IRenderPass* renderPass = nullptr;
        uint32_t     index      = 0;
    };
    AttachmentSpec attachments = {};
    RenderPassSpec subpass     = {};
};

enum class ERenderTargetDirtyReason : uint32_t
{
    None             = 0,
    Resize           = 1 << 0,
    FrameBufferCount = 1 << 1,
    Attachments      = 1 << 2,
    All              = 0xFFFFFFFFu,
};

inline ERenderTargetDirtyReason operator|(ERenderTargetDirtyReason lhs, ERenderTargetDirtyReason rhs)
{
    return static_cast<ERenderTargetDirtyReason>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline ERenderTargetDirtyReason& operator|=(ERenderTargetDirtyReason& lhs, ERenderTargetDirtyReason rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

struct IRenderTarget
{
    std::string       label          = "None";
    Extent2D          _extent        = {.width = 0, .height = 0};
    ERenderingMode::T _renderingMode = ERenderingMode::None;

    std::vector<AttachmentDescription>   _colorAttachmentDescs;
    std::optional<AttachmentDescription> _depthAttachmentDesc;
    std::optional<AttachmentDescription> _resolveAttachmentDesc;

    std::vector<stdptr<IFrameBuffer>> _frameBuffers;
    uint32_t                          _currentFrameIndex = 0;
    uint32_t                          _frameBufferCount  = 1;
    uint32_t                          _layerCount        = 1; // for array textures or cubemaps


    bool                     bDirty        = false;
    ERenderTargetDirtyReason _dirtyReason  = ERenderTargetDirtyReason::None;

    // opt
    bool         bSwapChainTarget              = false;
    int32_t      swapChianColorAttachmentIndex = 0;
    IRenderPass* _renderpass                   = nullptr;
    uint32_t     _subpassIndex                 = 0;

    MulticastDelegate<void()> onFramebufferRecreated;

    IRenderTarget()          = default;
    virtual ~IRenderTarget() = default;

    // Delete copy operations
    IRenderTarget(const IRenderTarget&)            = delete;
    IRenderTarget& operator=(const IRenderTarget&) = delete;

    // Default move operations
    IRenderTarget(IRenderTarget&&)            = default;
    IRenderTarget& operator=(IRenderTarget&&) = default;

    bool init(const RenderTargetCreateInfo& ci)
    {
        label                         = ci.label;
        bDirty                        = true;
        _dirtyReason                  = ERenderTargetDirtyReason::All;
        bSwapChainTarget              = ci.bSwapChainTarget;
        swapChianColorAttachmentIndex = ci.swapChianColorAttachmentIndex;
        _renderingMode                = ci.renderingMode;
        _frameBufferCount             = ci.frameBufferCount;
        _layerCount                   = ci.layerCount;

        if (_renderingMode == ERenderingMode::RenderPass)
        {
            _renderpass   = ci.subpass.renderPass;
            _subpassIndex = ci.subpass.index;
        }
        setExtent(ci.extent);
        bool ok = onInit(ci);
        if (ok) {
            bDirty       = false;
            _dirtyReason = ERenderTargetDirtyReason::None;
            if (_renderingMode == ERenderingMode::RenderPass) {
                for (auto fb : _frameBuffers)
                {
                    YA_CORE_ASSERT(fb->getHandle() != nullptr, "Frame buffer handle is null");
                }
            }
        }

        return ok;
    }
    virtual bool onInit(const RenderTargetCreateInfo& ci) = 0;
    virtual void recreate()                               = 0;
    virtual void destroy()                                = 0;

    // advance buffer index and execute begin render pass if needed
    virtual void beginFrame(ICommandBuffer* cmdBuf) = 0;
    virtual void endFrame(ICommandBuffer* cmdBuf)   = 0;

    void setExtent(Extent2D extent)
    {
        if (_extent.width != extent.width || _extent.height != extent.height)
        {
            _extent = extent;
            markDirty(ERenderTargetDirtyReason::Resize);
        }
    }

    // Flush dirty state: recreate images/framebuffers if needed.
    // Call this before using any RT images to ensure they match the current extent.
    void flushDirty()
    {
        if (bDirty) {
            recreate();
            bDirty       = false;
            _dirtyReason = ERenderTargetDirtyReason::None;
        }
    }

    void setFrameBufferCount(uint32_t frameBufferCount)
    {
        if (frameBufferCount == 0) {
            frameBufferCount = 1;
        }
        if (_frameBufferCount != frameBufferCount) {
            _frameBufferCount = frameBufferCount;
            markDirty(ERenderTargetDirtyReason::FrameBufferCount);
        }
    }

    bool setColorAttachmentSampleCount(uint32_t attachmentIndex, ESampleCount::T sampleCount)
    {
        if (attachmentIndex >= _colorAttachmentDescs.size()) {
            return false;
        }
        auto& desc = _colorAttachmentDescs[attachmentIndex];
        if (desc.samples != sampleCount) {
            desc.samples = sampleCount;
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
        return true;
    }

    bool setColorAttachmentFormat(uint32_t attachmentIndex, EFormat::T format)
    {
        if (attachmentIndex >= _colorAttachmentDescs.size()) {
            return false;
        }
        auto& desc = _colorAttachmentDescs[attachmentIndex];
        if (desc.format != format) {
            desc.format = format;
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
        return true;
    }

    bool setDepthAttachmentSampleCount(ESampleCount::T sampleCount)
    {
        if (!_depthAttachmentDesc.has_value()) {
            return false;
        }
        if (_depthAttachmentDesc->samples != sampleCount) {
            _depthAttachmentDesc->samples = sampleCount;
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
        return true;
    }

    bool setDepthAttachmentFormat(EFormat::T format)
    {
        if (!_depthAttachmentDesc.has_value()) {
            return false;
        }
        if (_depthAttachmentDesc->format != format) {
            _depthAttachmentDesc->format = format;
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
        return true;
    }

    bool setResolveAttachmentSampleCount(ESampleCount::T sampleCount)
    {
        if (!_resolveAttachmentDesc.has_value()) {
            return false;
        }
        if (_resolveAttachmentDesc->samples != sampleCount) {
            _resolveAttachmentDesc->samples = sampleCount;
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
        return true;
    }

    /// Set (or replace) the resolve attachment description. Marks dirty.
    void setResolveAttachment(const AttachmentDescription& desc)
    {
        _resolveAttachmentDesc = desc;
        markDirty(ERenderTargetDirtyReason::Attachments);
    }

    /// Remove the resolve attachment entirely. Marks dirty only if one existed.
    void clearResolveAttachment()
    {
        if (_resolveAttachmentDesc.has_value()) {
            _resolveAttachmentDesc.reset();
            markDirty(ERenderTargetDirtyReason::Attachments);
        }
    }

    void markDirty(ERenderTargetDirtyReason reason)
    {
        bDirty = true;
        _dirtyReason |= reason;
    }

    [[nodiscard]] ERenderTargetDirtyReason getDirtyReason() const { return _dirtyReason; }
    [[nodiscard]] bool hasDirtyReason(ERenderTargetDirtyReason reason) const
    {
        return (static_cast<uint32_t>(_dirtyReason) & static_cast<uint32_t>(reason)) != 0;
    }

    const Extent2D& getExtent() const { return _extent; }

    // ===== Attachment Access =====

    auto getCurFrameBuffer()
    {
        return _frameBuffers[getCurrentFrameIndex()].get();
    }
    auto getFrameBuffer(uint32_t index) { return _frameBuffers[index].get(); }

    bool isSwapChainTarget() const { return bSwapChainTarget; }

    uint32_t getCurrentFrameIndex() const { return _currentFrameIndex; }
    uint32_t getFrameBufferCount() const { return _frameBufferCount; }


    [[nodiscard]] IRenderPass* getRenderPass() const { return _renderpass; }

    virtual void onRenderGUI() {}

    ERenderingMode::T getRenderingMode() const { return _renderingMode; }

    uint32_t getSubpassIndex() const { return _subpassIndex; }

    const std::vector<AttachmentDescription>&   getColorAttachmentDescs() const { return _colorAttachmentDescs; }
    const std::optional<AttachmentDescription>& getDepthAttachmentDesc() const { return _depthAttachmentDesc; }
    const std::optional<AttachmentDescription>& getResolveAttachmentDesc() const { return _resolveAttachmentDesc; }
}; // namespace ya

/**
 * @brief Factory function to create a platform-specific render target
 */
std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetCreateInfo& ci);

} // namespace ya
