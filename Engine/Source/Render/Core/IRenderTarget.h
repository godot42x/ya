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
struct Texture;
struct RenderImage;



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
        bSwapChainTarget              = ci.bSwapChainTarget;
        swapChianColorAttachmentIndex = ci.swapChianColorAttachmentIndex;
        _renderingMode                = ci.renderingMode;
        _frameBufferCount             = ci.frameBufferCount == 0 ? 1 : ci.frameBufferCount;
        _layerCount                   = ci.layerCount;
        _extent                       = ci.extent;

        if (_renderingMode == ERenderingMode::RenderPass) {
            _renderpass   = ci.subpass.renderPass;
            _subpassIndex = ci.subpass.index;
        }
        bool ok = onInit(ci);
        if (ok) {
            if (_renderingMode == ERenderingMode::RenderPass) {
                for (auto fb : _frameBuffers) {
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

    const Extent2D& getExtent() const { return _extent; }

    // ===== Attachment Access =====

    auto getCurFrameBuffer()
    {
        return _frameBuffers[getCurrentFrameIndex()].get();
    }
    auto getFrameBuffer(uint32_t index) { return _frameBuffers[index].get(); }
    auto getCurFrameBuffer() const
    {
        return _frameBuffers[getCurrentFrameIndex()].get();
    }
    auto getFrameBuffer(uint32_t index) const { return _frameBuffers[index].get(); }

    Texture* getCurrentColorTexture(uint32_t attachmentIdx) const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getColorTexture(attachmentIdx) : nullptr;
    }

    RenderImage* getCurrentColorAttachment(uint32_t attachmentIdx) const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getColorAttachment(attachmentIdx) : nullptr;
    }

    std::shared_ptr<RenderImage> getCurrentColorAttachmentShared(uint32_t attachmentIdx) const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getColorAttachmentShared(attachmentIdx) : nullptr;
    }

    Texture* getCurrentDepthTexture() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getDepthTexture() : nullptr;
    }

    RenderImage* getCurrentDepthAttachment() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getDepthAttachment() : nullptr;
    }

    std::shared_ptr<RenderImage> getCurrentDepthAttachmentShared() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getDepthAttachmentShared() : nullptr;
    }

    Texture* getCurrentResolveTexture() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getResolveTexture() : nullptr;
    }

    RenderImage* getCurrentResolveAttachment() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getResolveAttachment() : nullptr;
    }

    std::shared_ptr<RenderImage> getCurrentResolveAttachmentShared() const
    {
        auto* frameBuffer = getCurFrameBuffer();
        return frameBuffer ? frameBuffer->getResolveAttachmentShared() : nullptr;
    }

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

} // namespace ya
