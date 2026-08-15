#pragma once

#include "Core/Base.h"

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Render.h"
#include "RHI/Shader.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/IRenderRuntimeServices.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Services/EnvironmentLightingResultProvider.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/Deferred/DeferredPipelineDebugViews.h"
#include "Render3D/Services/OffscreenTaskService.h"
#include "Render3D/Services/RenderDiagnosticsService.h"
#include "Render3D/Services/RenderSharedResourceProvider.h"
#include "Render3D/Services/GameplayResourceBinding.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

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
struct RenderImage;
struct IImage;
struct IImageView;
struct BasicPostprocessing;
struct DeferredRenderPipeline;
struct Sampler;
struct EnvironmentLightingComponent;
struct RenderFrameData;
struct DebugRenderSystem;
struct Node;

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

struct YA_RENDER_3D_API RenderRuntime : IRenderRuntimeServices
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
        /// Narrow host services injected by the Host; Render3D never locates
        /// App through globals.
        IRenderRuntimeHostServices*       hostServices = nullptr;
        IOffscreenTaskScheduler*          offscreenScheduler = nullptr;
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

    /// Presentation graph extension points recorded by the app. A single
    /// descriptor object keeps the presentation boundary explicit instead of
    /// threading several parallel callbacks through FrameInput.
    struct PresentationExtensions
    {
        std::function<void(ICommandBuffer*)>                         recordBeforeExtensions;
        std::function<void(ICommandBuffer*)>                         recordExtensions;
        std::function<bool(RenderGraph&, RGTextureHandle, Extent2D)> appendCapture;

        [[nodiscard]] bool empty() const
        {
            return !recordBeforeExtensions && !recordExtensions && !appendCapture;
        }
    };

    struct FrameInput
    {
        /// Optional module viewport composition (e.g. editor overlays). Recorded
        /// after the world graph and the runtime game UI pass, before the
        /// presentation graph; modules must not recreate GPU resources here.
        struct ViewportComposeExtensions
        {
            std::function<void(ICommandBuffer*)> recordCompose;

            [[nodiscard]] bool empty() const
            {
                return !recordCompose;
            }
        };

        struct OverlayInput
        {
            const std::vector<RenderOverlaySprite2D>* screenSprites = nullptr;
            const std::vector<RenderOverlaySprite3D>* worldSprites  = nullptr;
            const std::vector<RenderOverlayText2D>*   screenTexts   = nullptr;
        } overlay{};

        PresentationExtensions     presentationExtensions{};
        ViewportComposeExtensions  viewportCompose{};
        RenderPipelineFrameContext pipeline{};

        /// Immutable Game UI frame packet built before the graph; composited
        /// onto the final viewport image after the world graph. Command
        /// recording never touches the live widget tree.
        const UIFrameSnapshot* uiFrameSnapshot = nullptr;
    };

    IRenderRuntimeHostServices* _hostServices = nullptr;
    IOffscreenTaskScheduler*    _offscreenScheduler = nullptr;
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
    ERenderPipeline _renderPipeline      = ERenderPipeline::Deferred;
    ERenderPipeline _pendingRenderPipeline = ERenderPipeline::Deferred;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};

    Rect2D _viewportRect{};
    float  _viewportFrameBufferScale = 1.0f;
    bool   _bWorldSceneRenderEnabled = true;

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
    [[nodiscard]] GameplayResourceBinding*         getGameplayResourceBinding() const override;
    [[nodiscard]] EnvironmentLightingProcessor*  getEnvironmentLightingProcessor() const override;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] IImageView*                    getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*                    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                           isOffscreenPending() const { return _offscreen.isPending(); }
    [[nodiscard]] OffscreenTaskService&          getOffscreenTaskService() { return _offscreen; }
    [[nodiscard]] TerrainProcessor*               getTerrainProcessor() const { return _terrainProcessor.get(); }
    [[nodiscard]] const OffscreenTaskService&    getOffscreenTaskService() const { return _offscreen; }
    [[nodiscard]] RenderDiagnosticsService&      getDiagnosticsService() { return _diagnostics; }
    [[nodiscard]] const RenderDiagnosticsService& getDiagnosticsService() const { return _diagnostics; }

    // =========================================================================
    // Runtime outputs / debug inspection
    // =========================================================================
    [[nodiscard]] std::shared_ptr<RenderImage> getPostprocessOutputImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getActiveViewportImageShared() const;
    /// The image shown as the final viewport result: post-process output when
    /// present, otherwise the raw viewport output. Game UI composition, the
    /// presentation graph and the editor viewport snapshot must all read this
    /// single source so UI never renders into an image that is not presented.
    [[nodiscard]] std::shared_ptr<RenderImage> getViewportDisplayImageShared() const;
    /// Color format of the viewport display image, mirrored from
    /// getViewportDisplayImageShared() without needing the image to exist:
    /// the post-process output format when postprocessing is enabled, else the
    /// raw viewport color format. Pipeline-configured and stable per frame, so
    /// pre-recording pipeline prep can use it before the world graph runs.
    [[nodiscard]] EFormat::T getViewportDisplayImageFormat() const;
    [[nodiscard]] std::shared_ptr<RenderImage> getPresentationImageShared() const;
    [[nodiscard]] bool     isPostprocessingEnabled() const;
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _renderPipeline; }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pendingRenderPipeline; }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pendingRenderPipeline = renderPipeline; }
    void requestActivePipelineReload() { _pendingActivePipelineReload = true; }
    /// Enable/disable the world scene graph for the current frame. The editor
    /// 2D canvas mode disables it: only the UI compose pass and the editor
    /// viewport panel need rendering in that mode.
    void setWorldSceneRenderEnabled(bool bEnabled) { _bWorldSceneRenderEnabled = bEnabled; }
    [[nodiscard]] bool isWorldSceneRenderEnabled() const { return _bWorldSceneRenderEnabled; }

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
    void                   initDiagnostics(const InitDesc& desc);
    void                   initRenderBackend(const InitDesc& desc);
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
    void                   renderPresentationPass(float deltaTime,
                                                  const PresentationExtensions& presentationExtensions,
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
