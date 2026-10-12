#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Deferred/Features/GBufferFeature.h"
#include "Render3D/Deferred/Features/LightingFeature.h"
#include "Render3D/Deferred/Features/SSAOFeature.h"
#include "RHI/Core/DescriptorSet.h"

#include <optional>
#include <vector>

namespace ya
{

struct EnvironmentLightingSceneResources;
struct GBufferStage;
struct LightStage;
struct RenderFramePlan;
struct RenderStageContext;
struct SSAOStage;
struct ViewTargetLease;
class RenderSubmission;

/// Deferred pipeline composition recipe.
///
/// One resolved composition per View drives the whole Deferred slice:
///   GBuffer (geometry + depth) -> SSAO (optional AO) -> Lighting (SceneColor)
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
        GBufferStage* gBuffer = nullptr;
        SSAOStage*    ssao    = nullptr;
        LightStage*   light   = nullptr;
    };

    /// Pipeline-resolved configuration for one frame. Formats come from the
    /// pipeline's runtime formats; SSAO enablement and parameters come from the
    /// same settings snapshot the pipeline is about to apply.
    struct Config
    {
        SSAOFeature::Settings     ssao{};
        bool                      bReverseViewportY = true;
        DeferredAttachmentFormats gBufferFormats{};
        EFormat::T                viewColorFormat = EFormat::Undefined;
        EImageUsage::T            viewColorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    struct ResolvedView
    {
        uint64_t viewId     = 0;
        Extent2D viewExtent{};

        GBufferFeature::Resolved  gbuffer;
        SSAOFeature::Resolved     ssao;
        LightingFeature::Resolved lighting;
    };

    /// Submission-allocated bindings for one View, produced during recording
    /// where the submission exists.
    struct PreparedView
    {
        uint64_t viewId = 0;

        GBufferFeature::Prepared  gbuffer;
        SSAOFeature::Prepared     ssao;
        LightingFeature::Prepared lighting;

        /// Ranges of the View's imported host-written buffers, taken from the
        /// View binding; the RDG handles themselves are imported per graph.
        RGBufferRange frameRange{};
        RGBufferRange lightRange{};
        RGBufferRange ssaoFrameRange{};
    };

    struct PrepareContext
    {
        RenderSubmission*  submission = nullptr;
        DescriptorSetHandle frameAndLightDS{};
        DescriptorSetHandle skinningDS{};
        std::optional<DescriptorSetHandle> ssaoFrameDS{};
        DescriptorSetHandle environmentLightingDS{};
        /// Ranges of the View's imported host-written buffers, taken from the
        /// View binding.
        RGBufferRange frameRange{};
        RGBufferRange lightRange{};
        RGBufferRange ssaoFrameRange{};
    };

    struct AppendContext
    {
        RenderGraph&              graph;
        const RenderStageContext& stageCtx;
        const Stages*             stages    = nullptr;
        const ViewTargetLease*    viewLease = nullptr;

        /// Imported host-written buffers for this View.
        RGBufferHandle                frameBuffer{};
        RGBufferHandle                lightBuffer{};
        RGBufferHandle                skinningBuffer{};
        std::optional<RGBufferHandle> ssaoFrameBuffer{};

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
        GBufferFeature::GraphOutputs  gbuffer;
        SSAOFeature::GraphOutputs     ssao;
        LightingFeature::GraphOutputs lighting;
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
