#pragma once

#include "Core/Base.h"

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Render.h"
#include "RHI/Shader.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/HostClockState.h"
#include "Render3D/Services/EnvironmentLightingResultProvider.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/Debug/ViewportDebugCatalogBuilder.h"
#include "Render3D/Services/OffscreenTaskService.h"
#include "Render3D/Services/PipelineCoordinator.h"
#include "Render3D/Services/SurfacePresentation.h"
#include "Render3D/Services/RenderDiagnosticsService.h"
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
struct RenderViewSceneResources;
struct DebugRenderSystem;
struct Node;

/// Device-lifetime backend, persistent renderer services, fence-safe mutations,
/// and one frame's recording. The single owner of the recording: consuming a
/// sealed plan and writing commands into a submission are the same lifetime, so
/// `record` lives here instead of on a second object that would have to reach
/// back into this one for the submission table, the presentation graph and the
/// pipeline. Does not own the backend's swapchain and does not locate the active
/// Scene; every View binds its own Scene, and `record` accepts only an
/// `ExtractedSceneRender`, so a plan whose Scene content was never extracted
/// cannot be recorded.
struct YA_RENDER_3D_API RenderDeviceState
{
    using ERenderPipeline = PipelineCoordinator::ERenderPipeline;

    struct InitDesc
    {
        IRenderRuntimeHostServices*       hostServices = nullptr;
        IOffscreenTaskScheduler*          offscreenScheduler = nullptr;
        const HostClockState*    clockState = nullptr;
        EnvironmentLightingResultProvider environmentLightingProvider;
        uint32_t    windowWidth  = 0;
        uint32_t    windowHeight = 0;
        std::string windowTitle;
        bool        bEnableRenderDoc      = false;
        std::string renderDocDllPath;
        std::string renderDocCaptureOutputDir;
    };

    IRenderRuntimeHostServices* _hostServices = nullptr;
    IOffscreenTaskScheduler*    _offscreenScheduler = nullptr;
    const HostClockState* _clockState = nullptr;
    EnvironmentLightingResultProvider _environmentLightingProvider;
    std::unique_ptr<GameplayResourceBinding>       _gameplayResourceBinding;
    std::unique_ptr<EnvironmentLightingProcessor>  _environmentLightingProcessor;
    std::unique_ptr<TerrainProcessor>              _terrainProcessor;

    ut::StackDeleter _deleter;

    IRender*                                     _render = nullptr;
    OffscreenTaskService                         _offscreen{};
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    RenderSubmissionPool                         _submissions;
    RenderViewOutputTable                        _viewOutputs;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T  currentRenderAPI      = ERenderAPI::None;

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};
    PipelineCoordinator          _pipelineCoordinator{};
    /// One per OS-window present target this renderer has recorded for. Built
    /// on demand from `plan.present.surface` and torn down with the device, so a
    /// second window is a second entry here, not a second renderer. Every
    /// present destination a frame names must have one: the plan says which
    /// surface the frame presents, and this table is how that surface's images
    /// and its format-specific write pass are found.
    std::vector<std::unique_ptr<SurfacePresentation>> _surfacePresentations;
    /// The size a pipeline is built with before any View has declared one.
    /// Seeded once from the create-info window size; never a "current View
    /// rect" -- each View's size is that View's own declaration, and the view
    /// resources recorded for it are keyed by that View's identity.
    Extent2D                     _initialViewExtent{};

    /// Cached inspector catalog, rebuilt only when its digest changes. The
    /// snapshot is logically const, so this cache is too -- the state belongs to
    /// the presentation, not to frame execution.
    mutable ViewportDebugCatalogCache _viewportDebugCache{};

    void init(const InitDesc& desc);
    void shutdown(bool bRenderAlreadyIdle = false);

    /// Records graphics → UI → view compose → display compose for one sealed
    /// plan and returns what the host submits. The caller must already have
    /// acquired `plan.present` and submits what comes back: the recorded
    /// command buffer, or an empty frame when the result is invalid (see
    /// RecordedFrame).
    [[nodiscard]] RecordedFrame record(const RenderFramePlan& plan);

    /// Resolve the Scene-keyed GPU bindings one View's passes bind (skybox /
    /// IBL descriptor sets and derived resources). Called per View before
    /// recording starts, so a pass reads its own View's scene resources instead
    /// of asking this owner which Scene is current. Not const: binding a Scene's
    /// skybox/IBL descriptor set updates the cached binding for that set.
    void resolveViewSceneResources(Scene* scene, RenderViewSceneResources& out);
    /// Tick derived processors and rewrite IBL sets for this frame's Scene.
    void prepareDerivedState(Scene* scene, float dt);
    void prepareComposePipelines();

    void resetSkyboxPool();
    void resetEnvironmentLightingPool();

    [[nodiscard]] IRender*                       getRender() const { return _render; }
    [[nodiscard]] std::shared_ptr<ShaderStorage> getShaderStorage() const { return _shaderStorage; }
    [[nodiscard]] IRenderPipeline*               getActivePipeline() const;
    [[nodiscard]] uint64_t                       getHostTick() const;
    [[nodiscard]] double                         getElapsedTimeSeconds() const;
    [[nodiscard]] EnvironmentLightingProcessor*  getEnvironmentLightingProcessor() const;
    [[nodiscard]] bool                           isShadowMappingEnabled() const;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] OffscreenTaskService&          getOffscreenTaskService() { return _offscreen; }
    [[nodiscard]] TerrainProcessor*               getTerrainProcessor() const { return _terrainProcessor.get(); }
    [[nodiscard]] const OffscreenTaskService&    getOffscreenTaskService() const { return _offscreen; }
    [[nodiscard]] RenderDiagnosticsService&      getDiagnosticsService() { return _diagnostics; }
    [[nodiscard]] const RenderDiagnosticsService& getDiagnosticsService() const { return _diagnostics; }

    /// This surface's image for the index it acquired. The surface is a
    /// parameter because "the window's image" is only meaningful for a named
    /// window -- with more than one surface, an unnamed getter would have to
    /// pick one and call it the current one.
    [[nodiscard]] std::shared_ptr<RenderTexture> getPresentationImageShared(
        IRenderSurfaceContext& surface) const;
    [[nodiscard]] const RenderSubmission* getLiveSubmission(uint32_t flightIndex) const
    {
        return _submissions.get(flightIndex);
    }
    /// One View's output from one flight, or null when that flight published no
    /// such View.
    ///
    /// Both the View and the flight are parameters because the renderer has no
    /// opinion about which View matters: it publishes every View's output and
    /// names none of them "the current one". `flightIndex` is the value
    /// `RecordedFrame` handed back, so a reader that recorded the frame has it.
    [[nodiscard]] const RenderViewOutput* getViewOutput(uint32_t flightIndex, SceneViewId viewId) const;
    /// The image a surface pass may put on a window for this View, together with
    /// the fact its format cannot carry: which transfer function its values
    /// already have. A View with no output, or one whose display image is only
    /// its raw colour, comes back as an invalid image or as `Linear`.
    [[nodiscard]] FSurfaceImage surfaceImageFor(const RenderViewOutput* output) const;
    [[nodiscard]] bool     isGradingEnabled() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _pipelineCoordinator.getRenderPipeline(); }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pipelineCoordinator.getPendingRenderPipeline(); }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pipelineCoordinator.setPendingRenderPipeline(renderPipeline); }
    void requestActivePipelineReload() { _pipelineCoordinator.requestActivePipelineReload(); }

    /// Which strategy is active, and the settings it reads. Both are answered by
    /// the strategy itself through `IRenderPipeline`, so a caller that wants to
    /// display or change render settings does not have to downcast into
    /// `ForwardRenderPipeline` / `DeferredRenderPipeline` to find out what it is
    /// looking at. `resolveActivePipelineSettings` is the pending value when a
    /// request is in flight, which is what a settings panel needs to show.
    [[nodiscard]] ERenderPipeline          resolveActivePipelineKind() const;
    [[nodiscard]] RenderPipelineSettings   resolveActivePipelineSettings() const;
    void                                   requestActivePipelineSettings(const RenderPipelineSettings& settings);
    /// The compiled graph of the last recorded frame, or null when no pipeline
    /// has recorded one.
    [[nodiscard]] const RGTopologyDescription* getActiveFrameGraphTopology() const;

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _sharedResourceProvider.getSkyboxDescriptorPool(); }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _sharedResourceProvider.getSkyboxDescriptorSetLayout(); }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _sharedResourceProvider.getSkyboxSampler(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _sharedResourceProvider.getFallbackSkyboxDescriptorSet(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(); }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const;
    [[nodiscard]] DebugRenderSystem&           getDebugRenderSystem() const;

    /// Depth format of the active strategy's View targets. Undefined when no
    /// strategy is built, which is a case the caller has to handle anyway: it
    /// asks this to configure a compose pipeline before any View exists.
    [[nodiscard]] EFormat::T    getViewDepthFormat() const;
    [[nodiscard]] RenderTargetCatalog buildRenderTargetCatalog() const;
    /// The editor's viewport data for one View of one flight: the View's
    /// images plus the pipeline's debug catalogs. Both identities are
    /// parameters -- `viewId == 0` means "this frame showed no View", which is
    /// an answer, and the renderer will not substitute another View's images
    /// for it.
    [[nodiscard]] RenderViewportSnapshot buildViewportSnapshot(uint32_t            flightIndex,
                                                               SceneViewId         viewId,
                                                               Scene*              inspectScene = nullptr) const;
    [[nodiscard]] bool            isDeferredPipelineActive() const { return _pipelineCoordinator.isDeferredPipelineActive(); }
    void requestRenderTargetFormat(const RenderTargetFormatCommand& command);

  private:
    void recordViewFamilies(const RenderFramePlan& plan);
    /// Everything that mutates pipeline state or prepares GPU resources for this
    /// plan, before the command buffer opens: derived state for each Scene this
    /// plan renders, pending mutations, compose pipeline prep, each View's
    /// Scene-keyed GPU bindings, and the Game UI compose pipeline when the plan
    /// carries a UI snapshot. No View's geometry is applied here: a View sizes
    /// its own resources, and the plan is what names the Views this frame
    /// records. Split from
    /// `record` so "what happens before recording" and "what is recorded" are
    /// two readable steps instead of one 170-line function.
    void prepareFrameRecord(const RenderFramePlan& plan);

    void                   initRuntimeState(const InitDesc& desc);
    void                   initShaderSystems();
    void                   initDiagnostics(const InitDesc& desc);
    void                   initRenderBackend(const InitDesc& desc);
    void                   initResourceCaches();
    void                   initSharedRenderResources();
    void                   initSurfacePresentations();
    void                   initCommandResources();
    void                   initFrameServices();
    void                   shutdownRuntimeServices();
    void                   destroyRenderBackend();

    bool                   beginFrameCommandBuffer(const RenderFramePlan& plan, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult result);
    void                   retainPublishedViewOutputs(uint32_t flightIndex, ICommandBuffer* cmdBuf);
    void                   endFrameCommandBuffer(ICommandBuffer* cmdBuf);

    [[nodiscard]] SurfacePresentation* findSurfacePresentation(IRenderSurfaceContext& surface) const;
    /// Find or build the present target for `surface`. A safe-point action: it
    /// can construct a pipeline, so it runs in the pre-record section of
    /// `record` where `prepareComposePipelines` already does the same, and never
    /// while commands are being recorded.
    SurfacePresentation& acquireSurfacePresentation(IRenderSurfaceContext& surface);

    void buildViewportDebugCatalog(RenderViewportDebugCatalog& catalog, Scene* inspectScene) const;
    /// Resolve the handles this renderer is willing to expose to the inspector.
    [[nodiscard]] ViewportDebugCatalogInput makeViewportDebugCatalogInput(uint32_t   flightIndex,
                                                                         SceneViewId viewId,
                                                                         Scene*      inspectScene) const;
    /// Resolution points of `makeViewportDebugCatalogInput`, not part of the
    /// renderer's surface: their only consumers are the two functions above, so
    /// they are private until something outside the renderer needs them.
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog(uint32_t   flightIndex,
                                                                                  SceneViewId viewId) const;
    /// The deferred pipeline's recorded resources for the named View. Identity
    /// is the argument: the pipeline holds one View's resources per View, so
    /// there is no unnamed "current" set to hand back.
    [[nodiscard]] DeferredPipelineDebugViews getDeferredPipelineDebugViews(SceneViewId viewId) const;
};

} // namespace ya
