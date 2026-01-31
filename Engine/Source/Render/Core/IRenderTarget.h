#pragma once

#include "Core/Delegate.h"
#include "ECS/Entity.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/RenderDefines.h"
#include <memory>

#include <glm/glm.hpp>

namespace ya
{

/**
 * @brief Camera context data cached per-frame
 * Computed once per frame in App::onUpdate, used by all MaterialSystems
 * This decouples camera data acquisition from rendering systems
 */
struct FrameContext
{
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 cameraPos  = glm::vec3(0.0f);
};

/**
 * @brief Abstract interface for render targets
 * A render target represents a destination for rendering operations,
 * which can be the swapchain (screen) or an off-screen framebuffer.
 */
struct IRenderTarget
{
    std::string label   = "None";
    Extent2D    _extent = {0, 0};
    bool        bDirty  = false;

    // ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    // ClearValue depthClearValue = ClearValue(1.0f, 0);


    /**
     * @brief Triggered when framebuffer is recreated (e.g., resize, format change)
     * Listeners should invalidate any cached ImageView references
     */
    MulticastDelegate<void()> onFrameBufferRecreated;

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
    virtual void onRenderGUI();

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


    virtual void setFrameBufferCount(uint32_t count)                                      = 0;
    virtual void setColorClearValue(ClearValue clearValue)                                = 0;
    virtual void setColorClearValue(uint32_t attachmentIdx, ClearValue clearValue)        = 0;
    virtual void setDepthStencilClearValue(ClearValue clearValue)                         = 0;
    virtual void setDepthStencilClearValue(uint32_t attachmentIdx, ClearValue clearValue) = 0;

    // Getters
    virtual uint32_t                      getFrameBufferCount() const                                  = 0;
    [[nodiscard]] virtual IRenderPass    *getRenderPass() const                                        = 0;
    [[nodiscard]] virtual IFrameBuffer   *getFrameBuffer() const                                       = 0;
    [[nodiscard]] virtual uint32_t        getFrameBufferIndex() const                                  = 0; // Temp

    // Frame camera context methods
    // App is responsible for computing and setting camera context each frame
    virtual void                                     setFrameContext(const FrameContext &ctx) = 0;
    [[nodiscard]] virtual const FrameContext &getFrameContext() const                         = 0;

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
        auto rp     = getRenderPass();
        YA_CORE_ASSERT(rp, "Render pass is null when adding material system");
        system->onInit(rp);
        YA_CORE_DEBUG("Initialized material system: {}", system->_label);
        addMaterialSystemImpl(system);
    }

    virtual void             forEachMaterialSystem(std::function<void(std::shared_ptr<IMaterialSystem>)> func) = 0;
    virtual IMaterialSystem *getMaterialSystemByLabel(const std::string &label)                                = 0;

  protected:
    /**
     * @brief Implementation of adding material system (to be overridden)
     */
    virtual void addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system) = 0;
};



struct RenderTargetCreateInfo
{
    std::string  label;
    bool         bSwapChainTarget;
    IRenderPass *renderPass       = nullptr;
    uint32_t     frameBufferCount = 1;
    glm::vec2    extent           = {800.f, 600.f};
};

extern stdptr<IRenderTarget> createRenderTarget(RenderTargetCreateInfo ci);

} // namespace ya
