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
 * @brief Frame context containing per-frame camera data
 */
struct FrameContext
{
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 cameraPos  = glm::vec3(0.0f);
    glm::vec2 extent     = glm::vec2(0.0f);
};

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
    Extent2D          extent                        = {.width = 800, .height = 600};
    uint32_t          frameBufferCount              = 1; // for custom render targets

    struct AttachmentSpec
    {
        std::vector<AttachmentDescription> colorAttach = {}; // Support multiple color attachments
        AttachmentDescription              depthAttach = {}; // Undefined = no depth
    };

    struct RenderPassSpec
    {
        IRenderPass *renderPass = nullptr;
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

    std::vector<stdptr<IFrameBuffer>> _frameBuffers;
    uint32_t                          _currentFrameIndex = 0;

    bool bDirty = false;

    // opt
    bool         bSwapChainTarget              = false;
    int32_t      swapChianColorAttachmentIndex = 0;
    IRenderPass *_renderpass                   = nullptr;
    uint32_t     _subpassIndex                 = 0;

    MulticastDelegate<void()> onFramebufferRecreated;

    IRenderTarget()          = default;
    virtual ~IRenderTarget() = default;

    // Delete copy operations
    IRenderTarget(const IRenderTarget &)            = delete;
    IRenderTarget &operator=(const IRenderTarget &) = delete;

    // Default move operations
    IRenderTarget(IRenderTarget &&)            = default;
    IRenderTarget &operator=(IRenderTarget &&) = default;

    bool init(const RenderTargetCreateInfo &ci)
    {
        label                         = ci.label;
        bDirty                        = true;
        bSwapChainTarget              = ci.bSwapChainTarget;
        swapChianColorAttachmentIndex = ci.swapChianColorAttachmentIndex;
        _renderingMode                = ci.renderingMode;

        if (_renderingMode == ERenderingMode::RenderPass)
        {
            _renderpass   = ci.subpass.renderPass;
            _subpassIndex = ci.subpass.index;
        }
        setExtent(ci.extent);
        bool ok = onInit(ci);
        if (ok) {
            for (auto fb : _frameBuffers)
            {
                YA_CORE_ASSERT(fb->getHandle() != nullptr, "Frame buffer handle is null");
            }
        }

        return ok;
    }
    virtual bool onInit(const RenderTargetCreateInfo &ci) = 0;
    virtual void recreate()                               = 0;
    virtual void destroy()                                = 0;

    // advance buffer index and execute begin render pass if needed
    virtual void beginFrame(ICommandBuffer *cmdBuf) = 0;
    virtual void endFrame(ICommandBuffer *cmdBuf)   = 0;

    void setExtent(Extent2D extent)
    {
        if (_extent.width != extent.width || _extent.height != extent.height)
        {
            _extent = extent;
            bDirty  = true;
        }
    }

    const Extent2D &getExtent() const { return _extent; }

    // ===== Attachment Access =====

    auto getCurFrameBuffer()
    {
        return _frameBuffers[getCurrentFrameIndex()].get();
    }
    auto getFrameBuffer(uint32_t index) { return _frameBuffers[index].get(); }

    bool isSwapChainTarget() const { return bSwapChainTarget; }

    uint32_t getCurrentFrameIndex() const { return _currentFrameIndex; }
    uint32_t getFrameBufferCount() const { return _frameBuffers.size(); }


    [[nodiscard]] IRenderPass *getRenderPass() const { return _renderpass; }

    virtual void onRenderGUI() {}

    ERenderingMode::T getRenderingMode() const { return _renderingMode; }

    uint32_t getSubpassIndex() const { return _subpassIndex; }
}; // namespace ya

/**
 * @brief Factory function to create a platform-specific render target
 */
std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetCreateInfo &ci);

} // namespace ya
