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
#include "Render3D/Common/ViewTargetStore.h"
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
#include <span>

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
/// and the steps one frame's recording is made of. It owns the machinery, not
/// the arrangement: the *order* a product frame is recorded in -- which surface
/// this frame presents through, which View is the display root, how the plan's
/// composed Views become insets on it, and where the host's own stages sit -- is
/// the application's, in `GameRuntime/Render/RuntimeRenderContext`. Nothing on
/// this object answers "which Views does this frame render" or "which window
/// does it present"; the plan carries those and the application reads them.
/// Does not own the backend's swapchain and does not locate the active Scene;
/// every View binds its own Scene, and the recording steps accept only a
/// `RenderFramePlan` built from an `ExtractedSceneRender`, so a plan whose Scene
/// content was never extracted cannot be recorded.
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
    /// The window this device was created for (a startup surface). Kept so
    /// window-addressed questions -- a capture target, a diagnostic label -- can
    /// name their window instead of asking the device which surface it
    /// "is" (it has a set, and privileges none of them).
    INativeWindow*                               _startupWindow = nullptr;
    OffscreenTaskService                         _offscreen{};
    std::vector<std::shared_ptr<ICommandBuffer>> _commandBuffers;
    RenderSubmissionPool                         _submissions;
    ViewTargetStore                              _viewTargets;
    std::vector<RGTopologyDescription>           _frameGraphTopologies;
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

    /// Resolve the Scene-keyed GPU bindings one View's passes bind (skybox /
    /// IBL descriptor sets and derived resources). Called per View before
    /// recording starts, so a pass reads its own View's scene resources instead
    /// of asking this owner which Scene is current. Not const: binding a Scene's
    /// skybox/IBL descriptor set updates the cached binding for that set.
    void resolveViewSceneResources(Scene* scene, RenderViewSceneResources& out);
    /// Prepare the derived processors for exactly this frame's Scenes, then
    /// rewrite the IBL sets those Scenes asked for. The Scene set is the
    /// tick's declaration: a Scene it does not name has its derived state
    /// dropped.
    void prepareDerivedState(std::span<Scene* const> scenes, float dt);
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
    /// The recording in progress for `flightIndex`, or null when that flight is
    /// not recording. The mutable overload exists because recording *into* a
    /// flight is a step the host performs: a surface pass takes the recording as
    /// a `RenderSubmission&` (see `SurfacePresentation::recordDisplayCompose`) and
    /// the resources a host lifts while recording are kept alive through it.
    /// Which flight that is comes from the plan, so this answers a fact rather
    /// than choosing a recording.
    [[nodiscard]] RenderSubmission* getLiveSubmission(uint32_t flightIndex)
    {
        return _submissions.get(flightIndex);
    }
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
    /// Producer-driven View lifecycle: register when a producer attaches,
    /// unregister when it is removed. See ViewTargetStore for the exact
    /// lifetime contract.
    void registerSceneView(SceneViewKey key);
    void unregisterSceneView(SceneViewKey key);
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
    /// The graphs this submission compiled, one entry per recorded family.
    /// Cleared when the flight begins a new submission; consumers read the
    /// frame's family list, never a single family posing as the whole frame.
    [[nodiscard]] const std::vector<RGTopologyDescription>& getFrameGraphTopologies() const;

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
    /// The catalog's surface entry describes ONE window, which the caller
    /// names. It used to describe "the primary window" -- a fact the renderer
    /// cannot know and should not invent once several windows present.
    [[nodiscard]] RenderTargetCatalog buildRenderTargetCatalog(IRenderSurfaceContext& surface) const;
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

    // === The steps one frame's recording is made of ===
    //
    // The *order* these run in is the application's, spelled out once in
    // `GameRuntime/Render/RuntimeRenderContext`: present target, prepare, begin,
    // graphics, insets/UI, view compose, display compose, retain, end, seal.
    // Each function below is the real action of one such step. None of them is a
    // whole-frame entry point, and none of them decides which surface presents,
    // which View is the display root, or what a window shows -- the plan carries
    // those and the application reads them. There is deliberately no
    // `record(plan)`: one would put the product frame's arrangement back inside
    // the renderer.

    /// Everything that mutates pipeline state or prepares GPU resources for this
    /// plan, before the command buffer opens: derived state for each Scene this
    /// plan renders, pending mutations, compose pipeline prep, each View's
    /// Scene-keyed GPU bindings, and
    /// the Game UI compose pipeline when the plan carries a UI snapshot. No
    /// View's geometry is applied here: a View sizes its own resources, and the
    /// plan is what names the Views this frame records.
    void prepareFrameRecord(const RenderFramePlan& plan);
    /// Opens this plan's flight: begins its command buffer and opens the
    /// submission and the view-output table on it. False when there is nothing to
    /// record (the plan names no presentable surface / flight, or its flight has
    /// no command buffer), which is a legitimate frame the host submits empty.
    bool beginFrameCommandBuffer(const RenderFramePlan& plan, std::shared_ptr<ICommandBuffer>& cmdBuf);
    /// Hands each family of the plan to the active strategy, which records it
    /// into the flight's live submission and publishes its Views' outputs.
    void recordViewFamilies(const RenderFramePlan& plan);
    /// Close the flight's command buffer and publish the frame's GPU timing.
    void endFrameCommandBuffer(ICommandBuffer* cmdBuf);
    /// Seal the flight's submission and report the recording's identity: the
    /// command buffer to submit, the fence slot, and the serial. An invalid
    /// result means the recording must not be submitted -- its resources and
    /// finish state are what the fence slot expects.
    [[nodiscard]] RecordedFrame sealFrame(uint32_t flightIndex, ICommandBuffer* cmdBuf);
    /// Find or build the present target for the surface `id` names. A safe-point
    /// action: it can construct a pipeline, so it runs before any command is
    /// recorded, the same place `prepareComposePipelines` does. Which surface
    /// this frame presents through is not this renderer's question; the plan
    /// answers it, and the id is what the renderer files the answer under -- a
    /// surface pointer alone would attribute a reopened window's address to the
    /// previous window's imported images.
    SurfacePresentation& acquireSurfacePresentation(SurfaceId id, IRenderSurfaceContext& surface);

  private:
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

    void                   publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult result);

    [[nodiscard]] SurfacePresentation* findSurfacePresentation(SurfaceId id) const;
    /// The id this device's registry knows `surface` by, or an invalid id when
    /// the device does not own it (a stand-in surface in a test, or one whose
    /// registration is already gone).
    [[nodiscard]] SurfaceId surfaceIdOf(IRenderSurfaceContext& surface) const;
    /// Drop the present targets whose surface is no longer registered: their
    /// swapchain is gone, so their imported images and write pass describe a
    /// window that does not exist. A safe-point action, like the acquire above
    /// -- and the reason a frame presents through an id rather than an address.
    void reconcileSurfacePresentations();

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
};

} // namespace ya
