#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Composition/Features/BloomFeature.h"
#include "Render3D/Composition/Features/EntityIdFeature.h"
#include "Render3D/Composition/Features/FinalizeFeature.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Forward/Features/ForwardOpaqueFeature.h"
#include "Render3D/Forward/Features/ForwardSkyboxFeature.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Render3D/Forward/ForwardViewStage.h"

#include <optional>
#include <vector>

namespace ya
{

struct EntityIdPass;
struct FrameContext;
struct PostProcessingStage;
struct RenderFrameData;
struct RenderFramePlan;
struct RenderStageContext;
struct ViewTargetLease;
class RenderSubmission;

/// Forward pipeline composition recipe — the whole View map in one place.
///
/// One resolved composition per View drives the entire Forward frame:
///   opaque (clear + all shading models) -> skybox -> transparent slot
///   -> entity id -> bloom (optional) -> finalize.
///
/// The recipe is a map, not an owner: features own their passes and their device
/// objects, the View's prepared targets own the attachments, and the frame
/// import step owns the shared host-written buffers. The recipe states which
/// features run, in what order, and what data flows between them.
///
/// Three of the features on this map (Bloom, EntityId, Finalize) are the same
/// type the Deferred recipe uses: the protocol is shared, only the vocabulary
/// and the attachments differ.
///
/// # Adding a pass
///
/// 1. Create the feature under `Forward/Features/` (or `Composition/Features/`
///    when both pipelines use it) with its resolve/prepareBindings/append. If it
///    needs a View target, declare it in resolve as a `ViewTargetRequirement`;
///    `ResolvedView::collectViewTargets` publishes exactly what was declared.
/// 2. Wire it here in `appendGraph` (plus `resolve`/`prepareBindings`), in the
///    order it runs: the connections are this function, the ports are typed.
/// 3. A slot with no functional contract is declared by the map itself through
///    `composition::declareColorContinuation` (today: the transparent slot).
///
/// Failure stays loud: a feature whose target was not published fails its append
/// with `FeatureError::MissingDependency`, and any phase failure fails the View
/// instead of publishing a partial frame.
struct ForwardPipelineRecipe
{
    /// Device objects the features' passes need. Passed in, never reached for.
    struct Stages
    {
        ForwardViewStage*    viewStage = nullptr;
        EntityIdPass*        entityId  = nullptr;
        PostProcessingStage* post      = nullptr;
    };

    /// Pipeline-resolved configuration for one frame: the View's formats, this
    /// pipeline's graph vocabulary, and the shared optional features' settings.
    struct Config
    {
        bool           bEnabled = true;
        EFormat::T     viewColorFormat = EFormat::Undefined;
        EImageUsage::T viewColorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        EFormat::T     viewDepthFormat = EFormat::Undefined;
        EImageUsage::T viewDepthUsage  = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled;
        bool           bReverseViewportY = true;

        BloomFeature::Settings    bloom{};
        FinalizeFeature::Settings finalize{};
    };

    struct ResolvedView
    {
        uint64_t viewId     = 0;
        Extent2D viewExtent{};

        ForwardOpaqueFeature::Resolved opaque;
        ForwardSkyboxFeature::Resolved skybox;
        EntityIdFeature::Resolved      entityId;
        BloomFeature::Resolved         bloom;
        FinalizeFeature::Resolved      finalize;

        /// Every View target this composition needs, in the stable order the
        /// request builder publishes them. One declaration per fact: the same
        /// Resolved values gate the features' bindings and imports.
        [[nodiscard]] std::vector<ViewAttachmentDesc> collectViewTargets() const;
    };

    /// Submission-allocated bindings for one View, produced during recording
    /// where the submission exists.
    struct PreparedView
    {
        uint64_t viewId = 0;

        ForwardOpaqueFeature::Prepared opaque;
        ForwardSkyboxFeature::Prepared skybox;
        EntityIdFeature::Prepared      entityId;
        BloomFeature::Prepared         bloom;
        FinalizeFeature::Prepared      finalize;
    };

    struct PrepareContext
    {
        RenderSubmission*                      submission     = nullptr;
        IRender*                               render         = nullptr;
        uint32_t                               alignment      = 1;
        const RenderFrameData*                 frameData      = nullptr;
        /// The View binding: shared frame/light/skinning sets and slices.
        const ForwardFrameResourceSet::Binding* frameBinding = nullptr;
    };

    struct AppendContext
    {
        RenderGraph&              graph;
        const RenderStageContext& stageCtx;
        const Stages*             stages    = nullptr;
        const ViewTargetLease*    viewLease = nullptr;
        const ForwardFrameResourceSet::Binding* frameBinding = nullptr;
        /// The Forward stage's per-View pass context, built from the View's
        /// declaration before the graph.
        ForwardViewStage::PassContext* viewPassContext = nullptr;
        FrameContext*                  postContext     = nullptr;
        /// The View's extracted direction-gizmo snapshot (Forward only).
        std::vector<ForwardDirectionGizmoInput> directionGizmos{};
        /// The View's skybox packet (cubemap set + mesh), from its declaration.
        const ForwardViewAuxPasses::DrawContext::SkyboxInput* skybox = nullptr;

        /// The View's shared shadow output, imported by whoever appended the
        /// shadow passes.
        ShadowGraphOutputs shadow{};
        /// The View's resolved environment lighting. Imported here because this
        /// is the graph-owning step; features receive handles only.
        const EnvironmentLightingSceneResources* environmentLighting = nullptr;

        /// Explicit ordering point for this View's first pass.
        std::optional<RGPassHandle> predecessor{};
        bool                        bReverseViewportY = true;
    };

    struct GraphOutputs
    {
        ForwardOpaqueFeature::GraphOutputs opaque;
        ForwardSkyboxFeature::GraphOutputs skybox;
        EntityIdFeature::GraphOutputs      entityId;
        BloomFeature::GraphOutputs         bloom;
        FinalizeFeature::GraphOutputs      finalize;
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
