#pragma once

#include "Core/Debug/Instrumentor.h"
#include "ECS/Entity.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/RenderPass.h"
#include "Render/RenderDefines.h"


namespace ya
{

/**
 * @brief Vulkan-specific implementation of render target
 * Manages framebuffers, attachments, and rendering operations for Vulkan
 */
struct VulkanRenderTarget : public IRenderTarget
{
    VulkanRenderTarget(const VulkanRenderTarget &)            = delete;
    VulkanRenderTarget &operator=(const VulkanRenderTarget &) = delete;
    VulkanRenderTarget(VulkanRenderTarget &&)                 = delete;
    VulkanRenderTarget &operator=(VulkanRenderTarget &&)      = delete;

    IRenderPass *_renderPass       = nullptr;
    int          subpassRef        = -1; // TODO: a RT should related to a subpass
    uint32_t     _frameBufferCount = 0;

    std::vector<std::shared_ptr<IFrameBuffer>> _frameBuffers;
    std::vector<ClearValue>                    _clearValues;

    uint32_t _currentFrameIndex = 0; // Current frame index for this render target

    bool bSwapChainTarget = false; // Whether this render target is the swap chain target
    bool bBeginTarget     = false; // Whether this render target is the begin target

    std::vector<std::shared_ptr<IMaterialSystem>> _materialSystems;

    FrameContext _cameraContext; // Cached camera data per-frame

  public:

    VulkanRenderTarget(const RenderTargetCreateInfo &ci);

    virtual ~VulkanRenderTarget() override;

    // IRenderTarget interface implementation
    void init() override;
    void recreate() override;
    void destroy() override;
    void onUpdate(float deltaTime) override;
    void onRender(ICommandBuffer *cmdBuf) override
    {
        YA_PROFILE_FUNCTION()
        renderMaterialSystems(cmdBuf);
    }
    void onRenderGUI() override;

    void begin(ICommandBuffer *cmdBuf) override;
    void end(ICommandBuffer *cmdBuf) override;

    void setColorClearValue(ClearValue clearValue) override;
    void setColorClearValue(uint32_t attachmentIdx, ClearValue clearValue) override;
    void setDepthStencilClearValue(ClearValue clearValue) override;
    void setDepthStencilClearValue(uint32_t attachmentIdx, ClearValue clearValue) override;

    [[nodiscard]] IRenderPass  *getRenderPass() const override { return _renderPass; }
    [[nodiscard]] IFrameBuffer *getFrameBuffer() const override { return _frameBuffers[_currentFrameIndex].get(); }
    void                        setFrameBufferCount(uint32_t count) override;
    uint32_t                    getFrameBufferCount() const override { return _frameBufferCount; }
    uint32_t                    getFrameBufferIndex() const override { return _currentFrameIndex; } // Temp

    void renderMaterialSystems(ICommandBuffer *cmdBuf);

    // Frame camera context - set by App, used by MaterialSystems
    void                setFrameContext(const FrameContext &ctx) override { _cameraContext = ctx; }
    const FrameContext &getFrameContext() const override { return _cameraContext; }

  public:
    void forEachMaterialSystem(std::function<void(std::shared_ptr<IMaterialSystem>)> func) override
    {
        for (auto &system : _materialSystems) {
            func(system);
        }
    }

    IMaterialSystem *getMaterialSystemByLabel(const std::string &label) override
    {
        for (auto &system : _materialSystems) {
            if (system && system->_label == label) {
                return system.get();
            }
        }
        return nullptr;
    }


  protected:
    void addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system) override;
};

} // namespace ya
