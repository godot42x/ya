#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/RenderDefines.h"
#include "ECS/Entity.h"

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

    Entity *_camera;
    bool    bEntityCamera = true; // Whether to use the camera from the entity

  public:
    VulkanRenderTarget(IRenderPass *renderPass);
    VulkanRenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent);

    virtual ~VulkanRenderTarget() override;

    // IRenderTarget interface implementation
    void init() override;
    void recreate() override;
    void destroy() override;
    void onUpdate(float deltaTime) override;
    void onRender(ICommandBuffer *cmdBuf) override { renderMaterialSystems(cmdBuf); }
    void onRenderGUI() override;

    void begin(ICommandBuffer *cmdBuf) override;
    void end(ICommandBuffer *cmdBuf) override;

    void setBufferCount(uint32_t count) override;
    void setColorClearValue(ClearValue clearValue) override;
    void setColorClearValue(uint32_t attachmentIdx, ClearValue clearValue) override;
    void setDepthStencilClearValue(ClearValue clearValue) override;
    void setDepthStencilClearValue(uint32_t attachmentIdx, ClearValue clearValue) override;

    [[nodiscard]] IRenderPass  *getRenderPass() const override { return _renderPass; }
    [[nodiscard]] IFrameBuffer *getFrameBuffer() const override { return _frameBuffers[_currentFrameIndex].get(); }

    void renderMaterialSystems(ICommandBuffer *cmdBuf);

    Entity            *getCameraMut() override { return _camera; }
    const Entity      *getCamera() const override { return _camera; }
    void               setCamera(Entity *camera) override { _camera = camera; }
    [[nodiscard]] bool isUseEntityCamera() const override { return bEntityCamera; }

    const glm::mat4 getProjectionMatrix() const override;
    const glm::mat4 getViewMatrix() const override;
    void            getViewAndProjMatrix(glm::mat4 &view, glm::mat4 &proj) const override;

  protected:
    void addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system) override;
};

} // namespace ya
