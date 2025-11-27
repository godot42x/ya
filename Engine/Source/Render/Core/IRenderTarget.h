#pragma once

#include "ECS/Entity.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/RenderDefines.h"
#include <memory>

namespace ya
{

/**
 * @brief Abstract interface for render targets
 * A render target represents a destination for rendering operations,
 * which can be the swapchain (screen) or an off-screen framebuffer.
 */
struct IRenderTarget
{
    Extent2D _extent = {0, 0};
    bool     bDirty  = false;

    IRenderTarget()          = default;
    virtual ~IRenderTarget() = default;

    // Delete copy operations
    IRenderTarget(const IRenderTarget &)            = delete;
    IRenderTarget &operator=(const IRenderTarget &) = delete;

    // Default move operations
    IRenderTarget(IRenderTarget &&)            = default;
    IRenderTarget &operator=(IRenderTarget &&) = default;

    /**
     * @brief Initialize the render target
     */
    virtual void init() = 0;

    /**
     * @brief Recreate framebuffers (e.g., after window resize)
     */
    virtual void recreate() = 0;

    /**
     * @brief Destroy render target resources
     */
    virtual void destroy() = 0;

    /**
     * @brief Update render target (called every frame)
     */
    virtual void onUpdate(float deltaTime) = 0;

    /**
     * @brief Render all material systems
     */
    virtual void onRender(ICommandBuffer *cmdBuf) = 0;

    /**
     * @brief Render ImGui interface for this render target
     */
    virtual void onRenderGUI() = 0;

    /**
     * @brief Begin rendering to this render target
     * @param cmdBuf Command buffer handle
     */
    virtual void begin(ICommandBuffer *cmdBuf) = 0;

    /**
     * @brief End rendering to this render target
     * @param cmdBuf Command buffer handle
     */
    virtual void end(ICommandBuffer *cmdBuf) = 0;

    void setExtent(Extent2D extent)
    {
        _extent = extent;
        bDirty  = true;
    }
    const Extent2D &getExtent() const { return _extent; }


    virtual void setBufferCount(uint32_t count)                                           = 0;
    virtual void setColorClearValue(ClearValue clearValue)                                = 0;
    virtual void setColorClearValue(uint32_t attachmentIdx, ClearValue clearValue)        = 0;
    virtual void setDepthStencilClearValue(ClearValue clearValue)                         = 0;
    virtual void setDepthStencilClearValue(uint32_t attachmentIdx, ClearValue clearValue) = 0;
    virtual void setCamera(Entity *camera)                                                = 0;

    // Getters
    [[nodiscard]] virtual IRenderPass    *getRenderPass() const                                        = 0;
    [[nodiscard]] virtual IFrameBuffer   *getFrameBuffer() const                                       = 0;
    [[nodiscard]] virtual Entity         *getCameraMut()                                               = 0;
    [[nodiscard]] virtual const Entity   *getCamera() const                                            = 0;
    [[nodiscard]] virtual bool            isUseEntityCamera() const                                    = 0;
    [[nodiscard]] virtual const glm::mat4 getProjectionMatrix() const                                  = 0;
    [[nodiscard]] virtual const glm::mat4 getViewMatrix() const                                        = 0;
    virtual void                          getViewAndProjMatrix(glm::mat4 &view, glm::mat4 &proj) const = 0;

    /**
     * @brief Add a material system to this render target
     * @tparam T Material system type (must derive from IMaterialSystem)
     * @param args Constructor arguments for the material system
     */
    template <typename T, typename... Args>
    void addMaterialSystem(Args &&...args)
    {
        static_assert(std::is_base_of_v<IMaterialSystem, T>, "T must be derived from IMaterialSystem");
        auto system = makeShared<T>(std::forward<Args>(args)...);
        system->onInit(getRenderPass());
        addMaterialSystemImpl(system);
    }

    virtual void forEachMaterialSystem(std::function<void(std::shared_ptr<IMaterialSystem>)> func) = 0;

  protected:
    /**
     * @brief Implementation of adding material system (to be overridden)
     */
    virtual void addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system) = 0;
};

/**
 * @brief Factory function to create a platform-specific render target
 * @param renderPass The render pass to use
 * @return Platform-specific render target instance
 */
std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass);

/**
 * @brief Factory function to create a platform-specific render target with custom size
 * @param renderPass The render pass to use
 * @param frameBufferCount Number of framebuffers
 * @param extent Size of the render target
 * @return Platform-specific render target instance
 */
std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent);

} // namespace ya
