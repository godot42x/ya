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
#include "Render3D/Services/PresentationGraphService.h"
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
    uint32_t                                     _publishedOutputFlight = MAX_FLIGHTS_IN_FLIGHT;
    uint64_t                                     _publishedOutputViewId = 0;
    std::shared_ptr<ShaderStorage>               _shaderStorage = nullptr;

    ERenderAPI::T  currentRenderAPI      = ERenderAPI::None;

    RenderSharedResourceProvider  _sharedResourceProvider{};
    RenderDiagnosticsService     _diagnostics{};
    PipelineCoordinator          _pipelineCoordinator{};
    PresentationGraphService     _presentationGraphService{};
    // default rect for default rt creation
    Rect2D                       _pipelineViewRect{};

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

    /// Safe-point mutation: pipeline RT specs. Call before command recording.
    void applyViewResize(Rect2D rect);
    void applyPendingMutations();
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

    [[nodiscard]] std::shared_ptr<RenderTexture> getPostprocessOutputImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getActiveViewImageShared() const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getViewDisplayImageShared() const;
    [[nodiscard]] EFormat::T getViewDisplayImageFormat() const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getPresentationImageShared() const;
    [[nodiscard]] const RenderSubmission* getLiveSubmission(uint32_t flightIndex) const
    {
        return _submissions.get(flightIndex);
    }
    [[nodiscard]] const RenderViewOutput* getViewOutput(uint64_t viewId) const;
    /// Decide which View's published output the host viewport displays for
    /// `flightIndex`. `displayViewId == 0` clears it. Called once per recorded
    /// tick by the coordinator, from the plan's display root.
    void publishViewOutputIdentity(uint32_t flightIndex, SceneViewId displayViewId);
    [[nodiscard]] bool     isGradingEnabled() const;
    [[nodiscard]] RenderPipelineDebugOutputCatalog buildPipelineDebugOutputCatalog() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _pipelineCoordinator.getRenderPipeline(); }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pipelineCoordinator.getPendingRenderPipeline(); }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pipelineCoordinator.setPendingRenderPipeline(renderPipeline); }
    void requestActivePipelineReload() { _pipelineCoordinator.requestActivePipelineReload(); }

    [[nodiscard]] stdptr<IDescriptorPool>      getSkyboxDescriptorPool() const { return _sharedResourceProvider.getSkyboxDescriptorPool(); }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxDescriptorSetLayout() const { return _sharedResourceProvider.getSkyboxDescriptorSetLayout(); }
    [[nodiscard]] Sampler*                     getSkyboxSampler() const { return _sharedResourceProvider.getSkyboxSampler(); }
    [[nodiscard]] DescriptorSetHandle          getFallbackSkyboxDescriptorSet() const { return _sharedResourceProvider.getFallbackSkyboxDescriptorSet(); }
    [[nodiscard]] DescriptorSetHandle          getSceneSkyboxDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] stdptr<IDescriptorSetLayout> getEnvironmentLightingDescriptorSetLayout() const { return _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(); }
    [[nodiscard]] DescriptorSetHandle          getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr);
    [[nodiscard]] EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const;
    [[nodiscard]] DebugRenderSystem&           getDebugRenderSystem() const;

    [[nodiscard]] Extent2D      getViewExtent() const;
    [[nodiscard]] DeferredPipelineDebugViews getDeferredPipelineDebugViews() const;
    [[nodiscard]] RenderTargetCatalog buildRenderTargetCatalog() const;
    [[nodiscard]] RenderViewportSnapshot buildViewportSnapshot(Scene* inspectScene = nullptr) const;
    [[nodiscard]] bool            isDeferredPipelineActive() const { return _pipelineCoordinator.isDeferredPipelineActive(); }
    void requestRenderTargetFormat(const RenderTargetFormatCommand& command);

  private:
    void recordViewFamilies(const RenderFramePlan& plan);
    /// Everything that mutates pipeline state or prepares GPU resources for this
    /// plan, before the command buffer opens: derived state for each Scene this
    /// plan renders, pending mutations, the display root's viewport resize,
    /// compose pipeline prep, each View's Scene-keyed GPU bindings, and the Game
    /// UI compose pipeline when the plan carries a UI snapshot. Split from
    /// `record` so "what happens before recording" and "what is recorded" are
    /// two readable steps instead of one 170-line function.
    void prepareFrameRecord(const RenderFramePlan& plan, const SceneViewTask* displayRoot);

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

    bool                   beginFrameCommandBuffer(const RenderFramePlan& plan, std::shared_ptr<ICommandBuffer>& cmdBuf);
    void                   clearPublishedViewOutputs();
    void                   publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult result);
    void                   retainPublishedViewOutputs(uint32_t flightIndex, ICommandBuffer* cmdBuf);
    [[nodiscard]] const RenderViewOutput* publishedViewOutput() const;
    void                   endFrameCommandBuffer(ICommandBuffer* cmdBuf);

    void buildViewportDebugCatalog(RenderViewportDebugCatalog& catalog, Scene* inspectScene) const;
    /// Resolve the handles this renderer is willing to expose to the inspector.
    [[nodiscard]] ViewportDebugCatalogInput makeViewportDebugCatalogInput(Scene* inspectScene) const;
};

} // namespace ya
