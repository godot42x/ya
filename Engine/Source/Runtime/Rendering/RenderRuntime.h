#pragma once

#include "Core/Base.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Graph/RenderGraphExecutor.h"
#include "Render/Render.h"
#include "Render/Shader.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"
#include "Runtime/Rendering/Common/IRenderRuntimeServices.h"
#include "Runtime/Rendering/Common/PostProcessingState.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"
#include "Runtime/Rendering/Common/RenderTargetCatalog.h"
#include "Runtime/Rendering/Common/RenderViewportSnapshot.h"
#include "Runtime/Rendering/Deferred/DeferredPipelineDebugViews.h"
#include "Runtime/Rendering/Services/OffscreenTaskService.h"
#include "Runtime/Rendering/Services/RenderDiagnosticsService.h"
#include "Runtime/Rendering/Services/RenderSharedResourceProvider.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct App;
struct AppDesc;
enum AppMode : int;
struct SceneManager;
struct Scene;
struct ForwardRenderPipeline;
struct Texture;
struct RenderImage;
struct IImage;
struct IImageView;
struct BasicPostprocessing;
struct DeferredRenderPipeline;
struct Sampler;
struct EnvironmentLightingComponent;
struct RenderFrameData;
struct DebugRenderSystem;

struct RenderPipelineDebugOutputCatalog
{
    bool                     bShadowMappingEnabled  = false;
    std::shared_ptr<IImage> shadowDepthImage = nullptr;
    std::shared_ptr<RenderImage> viewportOutputImageOwner    = nullptr;
    std::shared_ptr<RenderImage> viewportDepthImageOwner     = nullptr;
    IImageView*                  shadowDirectionalDepth      = nullptr;
    std::shared_ptr<RenderImage> postprocessOutputImageOwner = nullptr;
    std::shared_ptr<RenderImage> bloomExtractOwner           = nullptr;
    std::shared_ptr<RenderImage> bloomBlurOwner              = nullptr;
    std::shared_ptr<RenderImage> bloomCompositeOwner         = nullptr;
    bool                         bPostprocessingEnabled      = false;
};

struct ENGINE_API RenderRuntime : IRenderRuntimeServices
{
    // =========================================================================
    // Public protocol
    // =========================================================================
    enum class ERenderPipeline
    {
        Forward,
        Deferred
    };

    struct InitDesc
    {
        App*           app     = nullptr;
        const AppDesc* appDesc = nullptr;
    };

    struct FrameInput
    {
        struct OverlayInput
        {
            const std::vector<RenderOverlaySprite2D>* screenSprites = nullptr;
            const std::vector<RenderOverlaySprite3D>* worldSprites  = nullptr;
            const std::vector<RenderOverlayText2D>*   screenTexts   = nullptr;
        } overlay{};

        struct AutomationInput
        {
            /// Append a presentation readback copy to the presentation graph.
            /// Called while the graph is still being built, before execute.
            std::function<bool(RenderGraph&, RGTextureHandle, Extent2D)> appendPresentationCapture;
        } automation{};

        std::function<void(ICommandBuffer*)> recordBeforePresentationExtensions;
        std::function<void(ICommandBuffer*)> recordPresentationExtensions;

        RenderPipelineFrameContext pipeline{};
    };

    App* _app = nullptr;

    ut::StackDeleter _deleter;

    IRender*                                     _render = nullptr;
    OffscreenTaskService                         _offscreen{};
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T  currentRenderAPI      = ERenderAPI::None;
    ERenderPipeline _renderPipeline      = ERenderPipeline::Deferred;
    ERenderPipeline _pendingRenderPipeline = ERenderPipeline::Deferred;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};

    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;

    std::vector<std::unique_ptr<RenderGraphExecutor>> _presentationGraphExecutors;
    std::vector<std::shared_ptr<RenderImage>>         _presentationImages;
    stdptr<BasicPostprocessing>                       _presentationPostProcessor = nullptr;
    PostProcessingState                               _presentationPostProcessState{};

    std::vector<RenderTargetFormatCommand> _pendingRenderTargetFormatCommands;
    bool                                   _pendingActivePipelineReload = false;
    mutable size_t _viewportDebugCatalogSignature = 0;
    mutable std::shared_ptr<RenderViewportDebugCatalog> _viewportDebugCatalog = nullptr;

    void init(const InitDesc& desc);
    void shutdown(bool bRenderAlreadyIdle = false);
    void renderFrame(const FrameInput& input);

  public:
    // =========================================================================
    // Runtime control / services
    // =========================================================================
    void onViewportResized(Rect2D rect);
    void resetSkyboxPool();
    void resetEnvironmentLightingPool();

    [[nodiscard]] IRender*                       getRender() const { return _render; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }
    [[nodiscard]] IRenderPipeline*               getActivePipeline() const;
    [[nodiscard]] uint64_t                       getFrameIndex() const override;
    [[nodiscard]] double                         getElapsedTimeSeconds() const override;
    [[nodiscard]] Scene*                         getActiveScene() const override;
    [[nodiscard]] ResourceResolveSystem*         getResourceResolveSystem() const override;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] IImageView*                    getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*                    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                           isOffscreenPending() const { return _offscreen.isPending(); }
    [[nodiscard]] OffscreenTaskService&          getOffscreenTaskService() { return _offscreen; }
    [[nodiscard]] const OffscreenTaskService&    getOffscreenTaskService() const { return _offscreen; }
    [[nodiscard]] RenderDiagnosticsService&      getDiagnosticsService() { return _diagnostics; }
    [[nodiscard]] const RenderDiagnosticsService& getDiagnosticsService() const { return _diagnostics; }

    // =========================================================================
    // Runtime outputs / debug inspection
    // =========================================================================
    [[nodiscard]] std::shared_ptr<RenderImage> getPostprocessOutputImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getActiveViewportImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getPresentationImageShared() const;
    [[nodiscard]] bool     isPostprocessingEnabled() const;
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _renderPipeline; }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pendingRenderPipeline; }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pendingRenderPipeline = renderPipeline; }
    void requestActivePipelineReload() { _pendingActivePipelineReload = true; }

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _sharedResourceProvider.getSkyboxDescriptorPool(); }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _sharedResourceProvider.getSkyboxDescriptorSetLayout(); }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _sharedResourceProvider.getSkyboxSampler(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _sharedResourceProvider.getFallbackSkyboxDescriptorSet(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr) override;
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(); }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr) override;
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const override;
    [[nodiscard]] DebugRenderSystem&           getDebugRenderSystem() const override;


    [[nodiscard]] const Rect2D& getViewportRect() const { return _viewportRect; }
    [[nodiscard]] float         getViewportFrameBufferScale() const { return _viewportFrameBufferScale; }
    [[nodiscard]] Extent2D      getViewportExtent() const;
    [[nodiscard]] DeferredPipelineDebugViews getDeferredPipelineDebugViews() const;
    [[nodiscard]] RenderTargetCatalog buildRenderTargetCatalog() const;
    [[nodiscard]] RenderViewportSnapshot buildViewportSnapshot() const;
    [[nodiscard]] bool            isDeferredPipelineActive() const { return _renderPipeline == ERenderPipeline::Deferred; }
    void requestRenderTargetFormat(const RenderTargetFormatCommand& command);

  private:
    // =========================================================================
    // Lifecycle / startup
    // =========================================================================
    void                   initRuntimeState(const InitDesc& desc);
    void                   initShaderSystems();
    void                   initDiagnostics(const AppDesc& appDesc);
    void                   initRenderBackend(const AppDesc& appDesc);
    void                   initResourceCaches();
    void                   initSharedRenderResources();
    void                   initPresentationResources();
    void                   rebuildPresentationImages();
    void                   initCommandResources();
    void                   initFrameServices();
    void                   shutdownRuntimeServices();
    void                   destroyRenderBackend();

    // =========================================================================
    // Per-frame orchestration
    // =========================================================================
    bool                   prepareFrame(const FrameInput& input, int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf);
    void                   ensureViewportRectInitialized(const FrameInput& input);
    bool                   beginFrameCommandBuffer(int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   beginViewportPassAndTickPipeline(const FrameInput& input, ICommandBuffer* cmdBuf);
    void                   renderViewportPassOverlays(const RenderPipelineFrameContext& pipelineFrame, const FrameInput::OverlayInput& overlay, ICommandBuffer* cmdBuf);
    void                   renderPresentationPass(float deltaTime,
                                                  const std::function<void(ICommandBuffer*)>& recordBeforePresentationExtensions,
                                                  const std::function<void(ICommandBuffer*)>& recordPresentationExtensions,
                                                  const std::function<bool(RenderGraph&, RGTextureHandle, Extent2D)>& appendPresentationCapture,
                                                  ICommandBuffer* cmdBuf);
    /// Presentation resources (per-swapchain-image executors + imported images)
    /// are intentionally kept independent from the world-frame executor:
    /// swapchain acquire/present and recreate stay outside the world graph.
    /// Capture readback is appended inside the presentation graph (FG-601/603).
    void                   submitFrame(int32_t imageIndex, ICommandBuffer* cmdBuf);

    // =========================================================================
    // Debug viewport catalog
    // =========================================================================
    void buildViewportDebugCatalog(RenderViewportDebugCatalog& catalog) const;
    void appendViewportDebugImages(std::vector<RenderViewportDebugImageSlot>& images,
                                   RenderViewportDebugCatalog*                catalog) const;
    [[nodiscard]] size_t buildViewportDebugCatalogSignature() const;
    void ensureViewportDebugCatalog() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getViewportSnapshotImageShared() const;

    // =========================================================================
    // Internal pipeline / presentation helpers
    // =========================================================================
    void                   initActivePipeline();
    void                   initForwardPipeline(int windowWidth, int windowHeight);
    void                   initDeferredPipeline(int windowWidth, int windowHeight);
    void                   shutdownActivePipeline();
    void                   applyPendingRenderPipelineSwitch();
    void                   applyPendingRenderTargetFormatCommands();
    [[nodiscard]] ForwardRenderPipeline*         getSelectedForwardPipeline() const;
    [[nodiscard]] DeferredRenderPipeline*        getSelectedDeferredPipeline() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getCurrentPresentationImageShared() const;
    [[nodiscard]] uint32_t                     getCurrentPresentationImageIndex() const;
};

} // namespace ya
