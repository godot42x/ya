#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/Features/SSAOFeature.h"
#include "Graph/RenderGraph.h"

#include <vector>

namespace ya
{

struct RenderFramePlan;
struct SceneViewRecording;
class ViewTargetStore;
class RenderSubmission;
struct IRender;

/// Deferred pipeline composition recipe.
/// 
/// Recipe composition order:
///   Shadow (external, prepared before composition)
///   → GBuffer (geometry + depth)
///   → SSAO (optional AO from GBuffer)
///   → Lighting (consumes GBuffer + OptionalAO + Shadow + Environment)
///   → Forward Opaque placeholder (attachment operation, no draw)
///   → Skybox
///   → Sprite2D
///   → Bloom (optional color transforms)
///   → Overlay (billboard + gizmo)
///   → EntityId (picking)
///   → Finalize (display color)
struct DeferredPipelineRecipe
{
    /// Resolved composition for one view.
    struct ResolvedView
    {
        uint64_t viewId = 0;
        
        // Feature resolved states
        SSAOFeature::Resolved ssao;
        
        // Resource requirements (collected from all features)
        struct ResourceRequirements
        {
            std::vector<ViewTargetRequest> viewTargets;
        };
        ResourceRequirements resources;
    };
    
    /// Prepared bindings for one view (consume resolved + submission resources).
    struct PreparedView
    {
        uint64_t viewId = 0;
        
        // Feature prepared bindings
        SSAOFeature::Prepared ssao;
    };
    
    /// Graph outputs for one view.
    struct GraphOutputs
    {
        SSAOFeature::OptionalAO ao;
        // More outputs will be added in C2-C5
    };
    
    /// Resolve composition for all views in the plan.
    /// Called at safe point before command recording.
    static Result<std::vector<ResolvedView>, FeatureError> resolve(
        IRender* render,
        ViewTargetStore* viewTargets,
        const RenderFramePlan& plan);
    
    /// Prepare bindings for all resolved views.
    /// Called after resolve, uses RenderSubmission for descriptor/upload allocation.
    static Result<std::vector<PreparedView>, FeatureError> prepareBindings(
        IRender* render,
        RenderSubmission* submission,
        ViewTargetStore* viewTargets,
        const std::vector<ResolvedView>& resolved,
        uint32_t flightIndex);
    
    /// Append graph passes for one view.
    /// Called during command recording, builds RenderGraph passes.
    static Result<GraphOutputs, FeatureError> appendGraph(
        RenderGraph& graph,
        IRender* render,
        ViewTargetStore* viewTargets,
        const ResolvedView& resolved,
        const PreparedView& prepared,
        const RenderStageContext& stageCtx);
};

} // namespace ya
