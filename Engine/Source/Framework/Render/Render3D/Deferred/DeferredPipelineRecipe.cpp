#include "Render3D/Deferred/DeferredPipelineRecipe.h"

#include "Core/Log.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Common/RenderSubmission.h"
#include "RHI/Render.h"

namespace ya
{

Result<std::vector<DeferredPipelineRecipe::ResolvedView>, FeatureError> 
DeferredPipelineRecipe::resolve(
    IRender* render,
    ViewTargetStore* viewTargets,
    const RenderFramePlan& plan)
{
    std::vector<ResolvedView> resolved;
    
    // C1: Minimal implementation with SSAO only
    // C2-C5 will add more features
    
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        if (!recording.task || !recording.frameData) {
            continue;
        }
        
        ResolvedView view;
        view.viewId = recording.task->desc.viewId;
        
        // Resolve SSAO feature
        SSAOFeature::Settings ssaoSettings;
        ssaoSettings.bEnabled = true; // C1: always enabled for testing
        // TODO: read from actual pipeline settings
        
        SSAOFeature::Facts ssaoFacts;
        ssaoFacts.viewId = view.viewId;
        ssaoFacts.viewExtent = Extent2D::fromVec2(recording.task->desc.outputRect.extent);
        ssaoFacts.frameData = recording.frameData;
        ssaoFacts.bReverseY = true;
        
        SSAOFeature::Inputs ssaoInputs;
        // C1: GBuffer inputs will come from GBufferFeature in C3
        // For now, assume they exist as ViewAttachments
        
        SSAOFeature feature; // TODO: get from device state
        view.ssao = feature.resolve(ssaoSettings, ssaoFacts, ssaoInputs);
        
        // Collect resource requirements
        if (view.ssao.aoTarget.has_value()) {
            ViewTargetRequest req;
            req.viewId = view.viewId;
            req.extent = ssaoFacts.viewExtent;
            req.attachments.push_back(ViewAttachmentDesc{
                .role = view.ssao.aoTarget->role,
                .format = view.ssao.aoTarget->format,
                .usage = view.ssao.aoTarget->usage,
                .samples = ESampleCount::Sample_1,
            });
            view.resources.viewTargets.push_back(req);
        }
        
        resolved.push_back(view);
    }
    
    return resolved;
}

Result<std::vector<DeferredPipelineRecipe::PreparedView>, FeatureError> 
DeferredPipelineRecipe::prepareBindings(
    IRender* render,
    RenderSubmission* submission,
    ViewTargetStore* viewTargets,
    const std::vector<ResolvedView>& resolved,
    uint32_t flightIndex)
{
    std::vector<PreparedView> prepared;
    prepared.reserve(resolved.size());
    
    for (const ResolvedView& resolvedView : resolved) {
        PreparedView view;
        view.viewId = resolvedView.viewId;
        
        // Prepare SSAO bindings
        SSAOFeature::PrepareContext ctx;
        ctx.viewTargets = viewTargets;
        ctx.submission = submission;
        ctx.flightIndex = flightIndex;
        ctx.viewId = resolvedView.viewId;
        
        SSAOFeature::PreparedInputs ssaoInputs;
        // C1: GBuffer RG handles will be passed from GBufferFeature in C3
        
        SSAOFeature feature; // TODO: get from device state
        auto ssaoPrepared = feature.prepareBindings(resolvedView.ssao, ssaoInputs, ctx);
        
        if (ssaoPrepared.isErr()) {
            YA_CORE_ERROR("Failed to prepare SSAO bindings for view {}", resolvedView.viewId);
            return ssaoPrepared.error();
        }
        
        view.ssao = ssaoPrepared.value();
        prepared.push_back(view);
    }
    
    return prepared;
}

Result<DeferredPipelineRecipe::GraphOutputs, FeatureError> 
DeferredPipelineRecipe::appendGraph(
    RenderGraph& graph,
    IRender* render,
    ViewTargetStore* viewTargets,
    const ResolvedView& resolved,
    const PreparedView& prepared,
    const RenderStageContext& stageCtx)
{
    GraphOutputs outputs;
    
    // C1: Minimal SSAO graph append
    // C2-C5 will add full pipeline
    
    SSAOFeature::FeatureGraphContext ctx;
    ctx.graph = &graph;
    ctx.stageCtx = &stageCtx;
    
    SSAOFeature::PreparedInputs ssaoInputs;
    // C1: GBuffer handles will come from GBufferFeature
    
    SSAOFeature feature; // TODO: get from device state
    auto ssaoOutputs = feature.append(resolved.ssao, prepared.ssao, ssaoInputs, ctx);
    
    if (ssaoOutputs.isErr()) {
        YA_CORE_ERROR("Failed to append SSAO graph for view {}", resolved.viewId);
        return ssaoOutputs.error();
    }
    
    outputs.ao = ssaoOutputs.value().ao;
    
    return outputs;
}

} // namespace ya
