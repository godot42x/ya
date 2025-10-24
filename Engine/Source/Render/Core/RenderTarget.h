
#pragma once

#include "ECS/Entity.h"
#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/RenderDefines.h"

namespace ya
{

struct VulkanRenderPass;

struct RenderTarget
{

    IRenderPass *_renderPass       = nullptr;
    int          subpassRef        = -1; // TODO: a RT should related to a subpass
    uint32_t     _frameBufferCount = 0;
    Extent2D     _extent           = {0, 0};

    std::vector<std::shared_ptr<IFrameBuffer>> _frameBuffers;
    std::vector<ClearValue>                    _clearValues;

    uint32_t _currentFrameIndex = 0; // Current frame index for this render target


    bool bSwapChainTarget = false; // Whether this render target is the swap chain target
    bool bBeginTarget     = false; // Whether this render target is the begin target

    bool bDirty = false;

    std::vector<std::shared_ptr<IMaterialSystem>> _materialSystems;

    Entity *_camera;
    bool    bEntityCamera = true; // Whether to use the camera from the entity


  public:

    // TODO : abstract API-independent class -> "IRenderPass"
    RenderTarget(IRenderPass *renderPass);
    RenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent);
    virtual ~RenderTarget()
    {
        destroy();
    }

    void init();
    void recreate();
    void destroy();
    void onUpdate(float deltaTime);
    void onRender(ICommandBuffer *cmdBuf) { renderMaterialSystems(cmdBuf); }
    void onRenderGUI();

    void begin(CommandBufferHandle cmdBuf);
    void end(CommandBufferHandle cmdBuf);

  public:
    void setExtent(Extent2D extent);
    void setBufferCount(uint32_t count);
    void setColorClearValue(ClearValue clearValue);
    void setColorClearValue(uint32_t attachmentIdx, ClearValue clearValue);

    void setDepthStencilClearValue(ClearValue clearValue);
    void setDepthStencilClearValue(uint32_t attachmentIdx, ClearValue clearValue);

    [[nodiscard]] IRenderPass  *getRenderPass() const { return _renderPass; }
    [[nodiscard]] IFrameBuffer *getFrameBuffer() const { return _frameBuffers[_currentFrameIndex].get(); }

    template <typename T, typename... Args>
    void addMaterialSystem(Args &&...args)
    {
        static_assert(std::is_base_of_v<IMaterialSystem, T>, "T must be derived from IMaterialSystem");
        auto system = makeShared<T>(std::forward<Args>(args)...);
        // TODO: abstract render API - change onInit to accept IRenderPass*
        system->onInit(getRenderPass());
        _materialSystems.push_back(system);
    }

    void renderMaterialSystems(ICommandBuffer *cmdBuf);

    Entity            *getCameraMut() { return _camera; }
    const Entity      *getCamera() const { return _camera; }
    void               setCamera(Entity *camera) { _camera = camera; }
    [[nodiscard]] bool isUseEntityCamera() const { return bEntityCamera; }

    const glm::mat4 getProjectionMatrix() const;
    const glm::mat4 getViewMatrix() const;
    void            getViewAndProjMatrix(glm::mat4 &view, glm::mat4 &proj) const;
};

}; // namespace ya