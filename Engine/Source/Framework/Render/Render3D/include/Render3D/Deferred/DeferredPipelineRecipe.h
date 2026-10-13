#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Composition/Features/BloomFeature.h"
#include "Render3D/Composition/Features/EntityIdFeature.h"
#include "Render3D/Composition/Features/FinalizeFeature.h"
#include "Render3D/Composition/Features/ShadowFeature.h"
#include "Render3D/Deferred/Features/GBufferFeature.h"
#include "Render3D/Deferred/Features/LightingFeature.h"
#include "Render3D/Deferred/Features/OverlayFeature.h"
#include "Render3D/Deferred/Features/SkyboxFeature.h"
#include "Render3D/Deferred/Features/Sprite2DFeature.h"
#include "Render3D/Deferred/Features/SSAOFeature.h"
#include "RHI/Core/DescriptorSet.h"

#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace ya
{

struct EnvironmentLightingSceneResources;
struct EntityIdPass;
struct FrameContext;
struct GBufferStage;
struct LightStage;
struct PostProcessingStage;
struct RenderFrameData;
struct RenderFramePlan;
struct RenderStageContext;
struct RenderViewRecordingContext;
struct SSAOStage;
struct ShadowStage;
struct Sprite2DStage;
struct ViewOverlayStage;
struct ViewTargetLease;
class RenderSubmission;

/// Deferred pipeline composition recipe — the whole View map in one place.
///
/// One resolved composition per View drives the entire Deferred frame:
///   GBuffer -> SSAO (optional) -> Lighting -> forward-opaque slot -> Skybox
///   -> Sprites -> Bloom (optional) -> Overlay -> EntityId -> Finalize.
///
/// The recipe is a map, not an owner: features own their passes and their
/// device objects, the View's prepared targets own the attachments, and the
/// frame import step owns the imported host-written buffers. The recipe states
/// which features run, in what order, and what data flows between them.
///
/// # Adding a pass
///
/// 1. Create the feature under `Deferred/Features/` with its
///    resolve/prepareBindings/append. If it needs a View target, declare it in
///    resolve as a `ViewTargetRequirement`; `ResolvedView::collectViewTargets`
///    publishes exactly what the features declared.
/// 2. Wire it here in `appendGraph` (plus `resolve`/`prepareBindings`), in the
///    order it runs: the connections are this function, the ports are typed.
/// 3. A slot with no functional contract is declared by the map itself through
///    `composition::declareColorContinuation` (today: the forward-opaque slot)
///    and graduates to a feature once it owns workload.
///
/// Failure stays loud: a feature whose target was not published fails its
/// append with `FeatureError::MissingDependency`, and any phase failure fails
/// the View instead of publishing a partial frame.
struct DeferredPipelineRecipe
{
    /// Device objects the features' passes need. Passed in, never reached for.
    struct Stages
    {
        ShadowStage*         shadow   = nullptr;
        GBufferStage*        gBuffer  = nullptr;
        SSAOStage*           ssao     = nullptr;
        LightStage*          light    = nullptr;
        /// One device object hosts both the skybox and the overlay pass.
        ViewOverlayStage*    overlay  = nullptr;
        Sprite2DStage*       sprites  = nullptr;
        EntityIdPass*        entityId = nullptr;
        PostProcessingStage* post     = nullptr;
    };

    /// Pipeline-resolved configuration for one frame. Formats come from the
    /// pipeline's runtime formats; SSAO/Bloom enablement and parameters come
    /// from the same settings snapshot the pipeline is about to apply.
    struct Config
    {
        ShadowFeature::Settings   shadow{};
        SSAOFeature::Settings     ssao{};
        BloomFeature::Settings    bloom{};
        FinalizeFeature::Settings finalize{};
        bool                      bReverseViewportY = true;
        DeferredAttachmentFormats gBufferFormats{};
        EFormat::T                viewColorFormat = EFormat::Undefined;
        EImageUsage::T            viewColorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    /// The map's features, in the order they run. One tuple member per feature:
    /// adding a feature adds a member here and its wiring in appendGraph; every
    /// phase struct and the folds below pick it up from this list.
    using FeatureInvocations = std::tuple<Invocation<ShadowFeature>,
                                          Invocation<GBufferFeature>,
                                          Invocation<SSAOFeature>,
                                          Invocation<LightingFeature>,
                                          Invocation<SkyboxFeature>,
                                          Invocation<Sprite2DFeature>,
                                          Invocation<BloomFeature>,
                                          Invocation<OverlayFeature>,
                                          Invocation<EntityIdFeature>,
                                          Invocation<FinalizeFeature>>;

    struct ResolvedView
    {
        uint64_t viewId     = 0;
        Extent2D viewExtent{};

        FeatureInvocations invocations{};

        // Accessor methods, NOT reference members: this type is stored in a
        // vector, and a reference member binds to the copy SOURCE's tuple during
        // copy construction — a dangling read once the local resolve() view goes
        // out of scope. Methods rebind per call, so copies stay self-contained.
        [[nodiscard]] auto& shadow()   { return std::get<Invocation<ShadowFeature>>(invocations).resolved; }
        [[nodiscard]] auto& gbuffer()  { return std::get<Invocation<GBufferFeature>>(invocations).resolved; }
        [[nodiscard]] auto& ssao()     { return std::get<Invocation<SSAOFeature>>(invocations).resolved; }
        [[nodiscard]] auto& lighting() { return std::get<Invocation<LightingFeature>>(invocations).resolved; }
        [[nodiscard]] auto& skybox()   { return std::get<Invocation<SkyboxFeature>>(invocations).resolved; }
        [[nodiscard]] auto& sprites()  { return std::get<Invocation<Sprite2DFeature>>(invocations).resolved; }
        [[nodiscard]] auto& bloom()    { return std::get<Invocation<BloomFeature>>(invocations).resolved; }
        [[nodiscard]] auto& overlay()  { return std::get<Invocation<OverlayFeature>>(invocations).resolved; }
        [[nodiscard]] auto& entityId() { return std::get<Invocation<EntityIdFeature>>(invocations).resolved; }
        [[nodiscard]] auto& finalize() { return std::get<Invocation<FinalizeFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& shadow()   const { return std::get<Invocation<ShadowFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& gbuffer()  const { return std::get<Invocation<GBufferFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& ssao()     const { return std::get<Invocation<SSAOFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& lighting() const { return std::get<Invocation<LightingFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& skybox()   const { return std::get<Invocation<SkyboxFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& sprites()  const { return std::get<Invocation<Sprite2DFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& bloom()    const { return std::get<Invocation<BloomFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& overlay()  const { return std::get<Invocation<OverlayFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& entityId() const { return std::get<Invocation<EntityIdFeature>>(invocations).resolved; }
        [[nodiscard]] const auto& finalize() const { return std::get<Invocation<FinalizeFeature>>(invocations).resolved; }

        /// Every View target this composition needs, folded from what each
        /// feature's Resolved declares. A feature missing from the fold cannot
        /// happen: the tuple is the registration.
        [[nodiscard]] std::vector<ViewAttachmentDesc> collectViewTargets() const;

        /// One line per feature, folded in map order with each feature's own
        /// name and enablement. Owning strings only.
        [[nodiscard]] std::string describe() const;
    };

    /// Submission-allocated bindings for one View, produced during recording
    /// where the submission exists.
    struct PreparedView
    {
        uint64_t viewId = 0;

        FeatureInvocations invocations{};

        // Accessor methods, not reference members (see ResolvedView).
        [[nodiscard]] auto& shadow()   { return std::get<Invocation<ShadowFeature>>(invocations).prepared; }
        [[nodiscard]] auto& gbuffer()  { return std::get<Invocation<GBufferFeature>>(invocations).prepared; }
        [[nodiscard]] auto& ssao()     { return std::get<Invocation<SSAOFeature>>(invocations).prepared; }
        [[nodiscard]] auto& lighting() { return std::get<Invocation<LightingFeature>>(invocations).prepared; }
        [[nodiscard]] auto& skybox()   { return std::get<Invocation<SkyboxFeature>>(invocations).prepared; }
        [[nodiscard]] auto& sprites()  { return std::get<Invocation<Sprite2DFeature>>(invocations).prepared; }
        [[nodiscard]] auto& bloom()    { return std::get<Invocation<BloomFeature>>(invocations).prepared; }
        [[nodiscard]] auto& overlay()  { return std::get<Invocation<OverlayFeature>>(invocations).prepared; }
        [[nodiscard]] auto& entityId() { return std::get<Invocation<EntityIdFeature>>(invocations).prepared; }
        [[nodiscard]] auto& finalize() { return std::get<Invocation<FinalizeFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& shadow()   const { return std::get<Invocation<ShadowFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& gbuffer()  const { return std::get<Invocation<GBufferFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& ssao()     const { return std::get<Invocation<SSAOFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& lighting() const { return std::get<Invocation<LightingFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& skybox()   const { return std::get<Invocation<SkyboxFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& sprites()  const { return std::get<Invocation<Sprite2DFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& bloom()    const { return std::get<Invocation<BloomFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& overlay()  const { return std::get<Invocation<OverlayFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& entityId() const { return std::get<Invocation<EntityIdFeature>>(invocations).prepared; }
        [[nodiscard]] const auto& finalize() const { return std::get<Invocation<FinalizeFeature>>(invocations).prepared; }

        /// Ranges of the View's shared imported host-written buffers, taken from
        /// the View binding; the RDG handles themselves are imported per graph.
        /// Feature-local UBOs (SSAO/skybox frames) travel in the features'
        /// prepared bindings instead.
        RGBufferRange frameRange{};
        RGBufferRange lightRange{};
    };

    struct PrepareContext
    {
        RenderSubmission*                      submission     = nullptr;
        IRender*                               render         = nullptr;
        uint32_t                               alignment      = 1;
        const RenderFrameData*                 frameData      = nullptr;
        /// The View's overlay packet. Mutable: billboard texture bindings are
        /// resolved into it while the overlay feature prepares.
        ViewOverlayStage::FrameInputs*         overlayInputs  = nullptr;
        /// The View binding: shared frame/light/skinning sets and slices.
        const DeferredFrameResourceSet::Binding* frameBinding = nullptr;
        /// The View's environment-lighting set (may be empty when the Scene has
        /// no resolved environment).
        DescriptorSetHandle                    environmentLightingDS{};
        /// The View the shadow feature prepares its per-View resources for.
        RenderViewRecordingContext             view{};
    };

    struct AppendContext
    {
        RenderGraph&              graph;
        const RenderStageContext& stageCtx;
        const Stages*             stages    = nullptr;
        const ViewTargetLease*    viewLease = nullptr;
        const DeferredFrameResourceSet::Binding* frameBinding = nullptr;
        /// The View's overlay packet, seen by the skybox/overlay/id passes.
        ViewOverlayStage::FrameInputs* overlayInputs = nullptr;
        FrameContext*                  postContext   = nullptr;

        /// Imported shared host-written buffers for this View. Feature-local
        /// UBOs (SSAO/skybox frames) are imported by their own features.
        RGBufferHandle                frameBuffer{};
        RGBufferHandle                lightBuffer{};
        RGBufferHandle                skinningBuffer{};

        /// The View's resolved environment lighting. Imported here because this
        /// is the graph-owning step; features receive handles only.
        const EnvironmentLightingSceneResources* environmentLighting = nullptr;

        /// Explicit ordering point for this View's first pass.
        std::optional<RGPassHandle> predecessor{};
        bool                        bReverseViewportY = true;
    };

    struct GraphOutputs
    {
        FeatureInvocations invocations{};

        // Accessor methods, not reference members (see ResolvedView).
        [[nodiscard]] auto& shadow()   { return std::get<Invocation<ShadowFeature>>(invocations).outputs; }
        [[nodiscard]] auto& gbuffer()  { return std::get<Invocation<GBufferFeature>>(invocations).outputs; }
        [[nodiscard]] auto& ssao()     { return std::get<Invocation<SSAOFeature>>(invocations).outputs; }
        [[nodiscard]] auto& lighting() { return std::get<Invocation<LightingFeature>>(invocations).outputs; }
        [[nodiscard]] auto& skybox()   { return std::get<Invocation<SkyboxFeature>>(invocations).outputs; }
        [[nodiscard]] auto& sprites()  { return std::get<Invocation<Sprite2DFeature>>(invocations).outputs; }
        [[nodiscard]] auto& bloom()    { return std::get<Invocation<BloomFeature>>(invocations).outputs; }
        [[nodiscard]] auto& overlay()  { return std::get<Invocation<OverlayFeature>>(invocations).outputs; }
        [[nodiscard]] auto& entityId() { return std::get<Invocation<EntityIdFeature>>(invocations).outputs; }
        [[nodiscard]] auto& finalize() { return std::get<Invocation<FinalizeFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& shadow()   const { return std::get<Invocation<ShadowFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& gbuffer()  const { return std::get<Invocation<GBufferFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& ssao()     const { return std::get<Invocation<SSAOFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& lighting() const { return std::get<Invocation<LightingFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& skybox()   const { return std::get<Invocation<SkyboxFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& sprites()  const { return std::get<Invocation<Sprite2DFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& bloom()    const { return std::get<Invocation<BloomFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& overlay()  const { return std::get<Invocation<OverlayFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& entityId() const { return std::get<Invocation<EntityIdFeature>>(invocations).outputs; }
        [[nodiscard]] const auto& finalize() const { return std::get<Invocation<FinalizeFeature>>(invocations).outputs; }

};

    /// Resolve one frame's composition: which features are on, what each needs,
    /// and the per-View values the later phases consume. Pure data: no GPU
    /// handles, no graph.
    [[nodiscard]] static Result<std::vector<ResolvedView>, FeatureError> resolve(const Config&          config,
                                                                                 const RenderFramePlan& plan);

    /// Prepare bindings for one resolved View using the live submission.
    [[nodiscard]] static Result<PreparedView, FeatureError> prepareBindings(const ResolvedView&   resolved,
                                                                             const Stages&         stages,
                                                                             const PrepareContext& ctx);

    /// Append the composition's passes for one View.
    [[nodiscard]] static Result<GraphOutputs, FeatureError> appendGraph(const ResolvedView& resolved,
                                                                        const PreparedView& prepared,
                                                                        const AppendContext& ctx);
};

} // namespace ya
