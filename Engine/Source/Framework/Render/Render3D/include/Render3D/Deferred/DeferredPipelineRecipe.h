#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Deferred/Features/BloomFeature.h"
#include "Render3D/Deferred/Features/EntityIdFeature.h"
#include "Render3D/Deferred/Features/FinalizeFeature.h"
#include "Render3D/Deferred/Features/ForwardOpaqueFeature.h"
#include "Render3D/Deferred/Features/GBufferFeature.h"
#include "Render3D/Deferred/Features/LightingFeature.h"
#include "Render3D/Deferred/Features/OverlayFeature.h"
#include "Render3D/Deferred/Features/SkyboxFeature.h"
#include "Render3D/Deferred/Features/Sprite2DFeature.h"
#include "Render3D/Deferred/Features/SSAOFeature.h"
#include "RHI/Core/DescriptorSet.h"

#include <optional>
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
struct SSAOStage;
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
struct DeferredPipelineRecipe
{
    /// Device objects the features' passes need. Passed in, never reached for.
    struct Stages
    {
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
        SSAOFeature::Settings     ssao{};
        BloomFeature::Settings    bloom{};
        FinalizeFeature::Settings finalize{};
        bool                      bReverseViewportY = true;
        DeferredAttachmentFormats gBufferFormats{};
        EFormat::T                viewColorFormat = EFormat::Undefined;
        EImageUsage::T            viewColorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    struct ResolvedView
    {
        uint64_t viewId     = 0;
        Extent2D viewExtent{};

        GBufferFeature::Resolved      gbuffer;
        SSAOFeature::Resolved         ssao;
        LightingFeature::Resolved     lighting;
        ForwardOpaqueFeature::Resolved forwardOpaque;
        SkyboxFeature::Resolved       skybox;
        Sprite2DFeature::Resolved     sprites;
        BloomFeature::Resolved        bloom;
        OverlayFeature::Resolved      overlay;
        EntityIdFeature::Resolved     entityId;
        FinalizeFeature::Resolved     finalize;

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

        GBufferFeature::Prepared  gbuffer;
        SSAOFeature::Prepared     ssao;
        LightingFeature::Prepared lighting;
        Sprite2DFeature::Prepared sprites;
        BloomFeature::Prepared    bloom;
        OverlayFeature::Prepared  overlay;
        EntityIdFeature::Prepared entityId;
        FinalizeFeature::Prepared finalize;

        /// Ranges of the View's imported host-written buffers, taken from the
        /// View binding; the RDG handles themselves are imported per graph.
        RGBufferRange frameRange{};
        RGBufferRange lightRange{};
        RGBufferRange ssaoFrameRange{};
        RGBufferRange skyboxFrameRange{};
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
        /// The View binding: frame/light/skinning/SSAO/skybox sets and slices.
        const DeferredFrameResourceSet::Binding* frameBinding = nullptr;
        /// The View's environment-lighting set (may be empty when the Scene has
        /// no resolved environment).
        DescriptorSetHandle                    environmentLightingDS{};
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

        /// Imported host-written buffers for this View.
        RGBufferHandle                frameBuffer{};
        RGBufferHandle                lightBuffer{};
        RGBufferHandle                skinningBuffer{};
        std::optional<RGBufferHandle> ssaoFrameBuffer{};
        RGBufferHandle                skyboxFrameBuffer{};

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
        GBufferFeature::GraphOutputs      gbuffer;
        SSAOFeature::GraphOutputs         ssao;
        LightingFeature::GraphOutputs     lighting;
        ForwardOpaqueFeature::GraphOutputs forwardOpaque;
        SkyboxFeature::GraphOutputs       skybox;
        Sprite2DFeature::GraphOutputs     sprites;
        BloomFeature::GraphOutputs        bloom;
        OverlayFeature::GraphOutputs      overlay;
        EntityIdFeature::GraphOutputs     entityId;
        FinalizeFeature::GraphOutputs     finalize;
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
