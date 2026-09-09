#pragma once

#include "Core/Base.h"

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Render.h"
#include "RHI/Shader.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/IRenderRuntimeServices.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Common/RenderRuntimeClockState.h"
#include "Render3D/Services/EnvironmentLightingResultProvider.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/Deferred/DeferredPipelineDebugViews.h"
#include "Render3D/Services/OffscreenTaskService.h"
#include "Render3D/Services/PipelineCoordinator.h"
#include "Render3D/Services/PresentationGraphService.h"
#include "Render3D/Services/RenderDiagnosticsService.h"
#include "Render3D/Services/ViewportStateService.h"
#include "Render3D/Services/RenderSharedResourceProvider.h"
#include "Render3D/Services/GameplayResourceBinding.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct IRenderRuntimeHostServices;
struct IOffscreenTaskScheduler;
struct SceneManager;
struct Scene;
struct EnvironmentLightingProcessor;
struct ForwardRenderPipeline;
struct Texture;
struct RenderTexture;
struct ImageResource;
struct BasicPostprocessing;
struct DeferredRenderPipeline;
struct Sampler;
struct EnvironmentLightingComponent;
struct RenderFrameData;
struct DebugRenderSystem;
struct Node;

struct RenderPipelineDebugOutputCatalog
{
    bool                          bShadowMappingEnabled         = false;
    std::shared_ptr<ImageResource> shadowDirectionalDepthResource = nullptr;
    std::shared_ptr<RenderTexture> viewportOutputImageOwner    = nullptr;
    std::shared_ptr<RenderTexture> viewportDepthImageOwner     = nullptr;
    std::shared_ptr<RenderTexture> postprocessOutputImageOwner = nullptr;
    std::shared_ptr<RenderTexture> bloomExtractOwner           = nullptr;
    std::shared_ptr<RenderTexture> bloomBlurOwner              = nullptr;
    std::shared_ptr<RenderTexture> bloomCompositeOwner         = nullptr;
    bool                         bPostprocessingEnabled      = false;
};

struct YA_RENDER_3D_API RenderRuntime : IRenderRuntimeServices
{
    using ERenderPipeline = PipelineCoordinator::ERenderPipeline;

    // =========================================================================
    // Public protocol
    // =========================================================================

    struct InitDesc
    {
        /// Narrow host services injected by the Host; Render3D never locates
        /// App through globals.
        IRenderRuntimeHostServices*       hostServices = nullptr;
        IOffscreenTaskScheduler*          offscreenScheduler = nullptr;
        const RenderRuntimeClockState*    clockState = nullptr;
        /// Injected narrow environment-lighting result provider (bound by the
        /// Host; Render3D never locates the processor through App).
        EnvironmentLightingResultProvider environmentLightingProvider;
        std::function<Scene*()>           activeSceneProvider;
        /// Presentation/app window metrics (copied from AppDesc by the Host).
        uint32_t    windowWidth  = 0;
        uint32_t    windowHeight = 0;
        std::string windowTitle;
        /// RenderDoc diagnostics knobs (copied from AppDesc by the Host).
        bool        bEnableRenderDoc      = false;
        std::string renderDocDllPath;
        std::string renderDocCaptureOutputDir;
    };

    /// One Camera chain plus one present surface for this host call.
    /// Grouping is typed; submit count stays one (R-1). The host acquires and
    /// presents via `FPresentFrame`; R-5 may split submits with evidence.
    struct FrameInput
    {
        CameraFrameInput    camera{};
        ViewComposeInput    viewCompose{};
        DisplayComposeInput displayCompose{};
        PresentFrameInput   present{};
    };

    IRenderRuntimeHostServices* _hostServices = nullptr;
    IOffscreenTaskScheduler*    _offscreenScheduler = nullptr;
    const RenderRuntimeClockState* _clockState = nullptr;
    EnvironmentLightingResultProvider _environmentLightingProvider;
    std::function<Scene*()>           _activeSceneProvider;
    /// Owned derived-processing systems (gameplay binding / environment
    /// lighting / terrain); ticked by renderFrame. Created by Render3D so
    /// the module never reaches the Host to locate them.
    std::unique_ptr<GameplayResourceBinding>       _gameplayResourceBinding;
    std::unique_ptr<EnvironmentLightingProcessor>  _environmentLightingProcessor;
    std::unique_ptr<TerrainProcessor>              _terrainProcessor;

    ut::StackDeleter _deleter;

    IRender*                                     _render = nullptr;
    OffscreenTaskService                         _offscreen{};
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T  currentRenderAPI      = ERenderAPI::None;

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};
    PipelineCoordinator          _pipelineCoordinator{};
    PresentationGraphService     _presentationGraphService{};
    ViewportStateService         _viewportState{};

    mutable size_t _viewportDebugCatalogSignature = 0;
    mutable std::shared_ptr<RenderViewportDebugCatalog> _viewportDebugCatalog = nullptr;

    void init(const InitDesc& desc);
    void shutdown(bool bRenderAlreadyIdle = false);
    /// Records graphics → UI → view compose → display compose. Caller must
    /// already have acquired `input.present` and must `submitPresentFrame`
    /// with the returned command buffer (or an empty list if null).
    [[nodiscard]] ICommandBuffer* renderFrame(const FrameInput& input);

  public:
    // =========================================================================
    // Runtime control / services
    // =========================================================================
    /// Resize the single WorldView[0] offscreen target. Not the present surface.
    void onViewportResized(Rect2D rect);
    void resetSkyboxPool();
    void resetEnvironmentLightingPool();

    [[nodiscard]] IRender*                       getRender() const { return _render; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }
    [[nodiscard]] IRenderPipeline*               getActivePipeline() const;
    [[nodiscard]] uint64_t                       getFrameIndex() const override;
    [[nodiscard]] double                         getElapsedTimeSeconds() const override;
    [[nodiscard]] Scene*                         getActiveScene() const override;
    [[nodiscard]] GameplayResourceBinding*         getGameplayResourceBinding() const override;
    [[nodiscard]] EnvironmentLightingProcessor*  getEnvironmentLightingProcessor() const override;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                           isOffscreenPending() const { return _offscreen.isPending(); }
    [[nodiscard]] OffscreenTaskService&          getOffscreenTaskService() { return _offscreen; }
    [[nodiscard]] TerrainProcessor*               getTerrainProcessor() const { return _terrainProcessor.get(); }
    [[nodiscard]] const OffscreenTaskService&    getOffscreenTaskService() const { return _offscreen; }
    [[nodiscard]] RenderDiagnosticsService&      getDiagnosticsService() { return _diagnostics; }
    [[nodiscard]] const RenderDiagnosticsService& getDiagnosticsService() const { return _diagnostics; }

    // =========================================================================
    // Runtime outputs / debug inspection
    // =========================================================================
    [[nodiscard]] std::shared_ptr<RenderTexture> getPostprocessOutputImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getActiveViewportImageShared() const;
    /// Output of this Camera chain after graphics + UI + view compose (post
    /// when enabled, else raw world color). Not the OS window / swapchain image.
    [[nodiscard]] std::shared_ptr<RenderTexture> getViewportDisplayImageShared() const;
    /// Format of that camera display RT, known before the world graph creates it.
    [[nodiscard]] EFormat::T getViewportDisplayImageFormat() const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getPresentationImageShared() const;
    [[nodiscard]] bool     isPostprocessingEnabled() const;
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _pipelineCoordinator.getRenderPipeline(); }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pipelineCoordinator.getPendingRenderPipeline(); }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pipelineCoordinator.setPendingRenderPipeline(renderPipeline); }
    void requestActivePipelineReload() { _pipelineCoordinator.requestActivePipelineReload(); }
    /// Enable/disable the world scene graph for the current frame. The editor
    /// 2D canvas mode disables it: only the UI compose pass and the editor
    /// viewport panel need rendering in that mode.
    void setWorldSceneRenderEnabled(bool bEnabled) { _viewportState.setWorldSceneRenderEnabled(bEnabled); }
    [[nodiscard]] bool isWorldSceneRenderEnabled() const { return _viewportState.isWorldSceneRenderEnabled(); }

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _sharedResourceProvider.getSkyboxDescriptorPool(); }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _sharedResourceProvider.getSkyboxDescriptorSetLayout(); }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _sharedResourceProvider.getSkyboxSampler(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _sharedResourceProvider.getFallbackSkyboxDescriptorSet(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr) override;
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(); }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr) override;
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const override;
    [[nodiscard]] DebugRenderSystem&           getDebugRenderSystem() const override;


    [[nodiscard]] const Rect2D& getViewportRect() const { return _viewportState.getRect(); }
    [[nodiscard]] float         getViewportFrameBufferScale() const { return _viewportState.getFrameBufferScale(); }
    void                        setViewportFrameBufferScale(float scale) { _viewportState.setFrameBufferScale(scale); }
    [[nodiscard]] Extent2D      getViewportExtent() const;
    [[nodiscard]] DeferredPipelineDebugViews getDeferredPipelineDebugViews() const;
    [[nodiscard]] RenderTargetCatalog buildRenderTargetCatalog() const;
    [[nodiscard]] RenderViewportSnapshot buildViewportSnapshot() const;
    [[nodiscard]] bool            isDeferredPipelineActive() const { return _pipelineCoordinator.isDeferredPipelineActive(); }
    void requestRenderTargetFormat(const RenderTargetFormatCommand& command);

  private:
    // =========================================================================
    // Lifecycle / startup
    // =========================================================================
    void                   initRuntimeState(const InitDesc& desc);
    void                   initShaderSystems();
    void                   initDiagnostics(const InitDesc& desc);
    void                   initRenderBackend(const InitDesc& desc);
    void                   initResourceCaches();
    void                   initSharedRenderResources();
    void                   initPresentationResources();
    void                   initCommandResources();
    void                   initFrameServices();
    void                   shutdownRuntimeServices();
    void                   destroyRenderBackend();

    // =========================================================================
    // Per-frame orchestration
    // =========================================================================
    bool                   prepareFrame(const FrameInput& input, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf);
    void                   ensureViewportRectInitialized(const FrameInput& input);
    bool                   beginFrameCommandBuffer(const FrameInput& input, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   beginViewportPassAndTickPipeline(const FrameInput& input, ICommandBuffer* cmdBuf);
    /// Ends GPU timing and the flight command buffer. Present stays on the
    /// host `FPresentFrame` coordinator (R-4).
    void                   endFrameCommandBuffer(ICommandBuffer* cmdBuf);

    // =========================================================================
    // Debug viewport catalog
    // =========================================================================
    void buildViewportDebugCatalog(RenderViewportDebugCatalog& catalog) const;
    void appendViewportDebugImages(std::vector<RenderViewportDebugImageSlot>& images,
                                   RenderViewportDebugCatalog*                catalog) const;
    [[nodiscard]] size_t buildViewportDebugCatalogSignature() const;
    void ensureViewportDebugCatalog() const;

    // =========================================================================
    // Internal pipeline / presentation helpers
    // =========================================================================
};

} // namespace ya
