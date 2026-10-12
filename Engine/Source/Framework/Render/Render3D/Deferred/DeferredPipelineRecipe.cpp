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
    
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        if (!recording.task || !recording.frameData) {
            continue;
        }
        
        ResolvedView view;
        view.viewId = recording.task->desc.viewId;
        Extent2D viewExtent = Extent2D::fromVec2(recording.task->desc.outputRect.extent);
        
        // 1. Resolve GBufferFeature
        GBufferFeature::Settings gbufferSettings;
        gbufferSettings.bEnabled = true;
        
        GBufferFeature::Facts gbufferFacts;
        gbufferFacts.viewId = view.viewId;
        gbufferFacts.viewExtent = viewExtent;
        gbufferFacts.frameData = recording.frameData;
        gbufferFacts.flightIndex = plan.frame.flightIndex;
        
        GBufferFeature gbufferFeature;
        view.gbuffer = gbufferFeature.resolve(gbufferSettings, gbufferFacts, GBufferFeature::Inputs{});
        
        // Collect GBuffer resource requirements
        ViewTargetRequest gbufferReq;
        gbufferReq.viewId = view.viewId;
        gbufferReq.pipeline = ERenderPipelineKind::Deferred;
        gbufferReq.extent = viewExtent;
        
        // GBuffer0-3 + SceneDepth
        for (const auto& [role, format, usage] : {
            std::tuple{view.gbuffer.outputs.albedoRole, EFormat::R8G8B8A8_UNORM, EImageUsage::ColorAttachment | EImageUsage::Sampled},
            std::tuple{view.gbuffer.outputs.normalRole, EFormat::R16G16B16A16_SFLOAT, EImageUsage::ColorAttachment | EImageUsage::Sampled},
            std::tuple{view.gbuffer.outputs.ormRole, EFormat::R8G8B8A8_UNORM, EImageUsage::ColorAttachment | EImageUsage::Sampled},
            std::tuple{view.gbuffer.outputs.shadingRole, EFormat::R8G8B8A8_UNORM, EImageUsage::ColorAttachment | EImageUsage::Sampled},
            std::tuple{view.gbuffer.outputs.depthRole, EFormat::D32_SFLOAT, EImageUsage::DepthStencilAttachment | EImageUsage::Sampled},
        }) {
            gbufferReq.attachments.push_back(ViewAttachmentDesc{
                .role = role,
                .format = format,
                .usage = usage,
                .samples = ESampleCount::Sample_1,
            });
        }
        view.resources.viewTargets.push_back(gbufferReq);
        
        // 2. Resolve SSAOFeature
        SSAOFeature::Settings ssaoSettings;
        ssaoSettings.bEnabled = true; // TODO: read from pipeline settings
        
        SSAOFeature::Facts ssaoFacts;
        ssaoFacts.viewId = view.viewId;
        ssaoFacts.viewExtent = viewExtent;
        ssaoFacts.frameData = recording.frameData;
        ssaoFacts.bReverseY = true;
        
        SSAOFeature::Inputs ssaoInputs;
        // GBuffer outputs will be wired in appendGraph
        
        view.ssao = SSAOFeature{}.resolve(ssaoSettings, ssaoFacts, ssaoInputs);
        
        // Collect SSAO resource requirements
        if (view.ssao.aoTarget.has_value()) {
            ViewTargetRequest ssaoReq;
            ssaoReq.viewId = view.viewId;
            ssaoReq.pipeline = ERenderPipelineKind::Deferred;
            ssaoReq.extent = viewExtent;
            ssaoReq.attachments.push_back(ViewAttachmentDesc{
                .role = view.ssao.aoTarget->role,
                .format = view.ssao.aoTarget->format,
                .usage = view.ssao.aoTarget->usage,
                .samples = ESampleCount::Sample_1,
            });
            view.resources.viewTargets.push_back(ssaoReq);
        }
        
        // 3. Resolve LightingFeature
        LightingFeature::Settings lightingSettings;
        lightingSettings.bEnabled = true;
        
        LightingFeature::Facts lightingFacts;
        lightingFacts.viewId = view.viewId;
        lightingFacts.viewExtent = viewExtent;
        lightingFacts.bReverseY = true;
        
        LightingFeature::Inputs lightingInputs;
        // GBuffer + SSAO outputs will be wired in appendGraph
        
        view.lighting = LightingFeature::resolve(lightingSettings, lightingFacts, lightingInputs);
        
        // Collect Lighting resource requirements
        ViewTargetRequest lightingReq;
        lightingReq.viewId = view.viewId;
        lightingReq.pipeline = ERenderPipelineKind::Deferred;
        lightingReq.extent = viewExtent;
        lightingReq.attachments.push_back(ViewAttachmentDesc{
            .role = view.lighting.outputs.sceneColorRole,
            .format = view.lighting.outputs.sceneColorFormat,
            .usage = view.lighting.outputs.sceneColorUsage,
            .samples = ESampleCount::Sample_1,
        });
        view.resources.viewTargets.push_back(lightingReq);
        
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
        
        // Prepare GBuffer bindings (frame/light descriptors)
        GBufferFeature gbufferFeature;
        view.gbuffer = gbufferFeature.prepareBindings(
            nullptr, // DeferredFrameResourceSet - will be passed in C3
            GBufferFeature::Facts{
                .viewId = resolvedView.viewId,
                .flightIndex = flightIndex,
            },
            resolvedView.gbuffer);
        
        // Prepare SSAO bindings
        SSAOFeature::PrepareContext ssaoCtx;
        ssaoCtx.viewTargets = viewTargets;
        ssaoCtx.submission = submission;
        ssaoCtx.flightIndex = flightIndex;
        ssaoCtx.viewId = resolvedView.viewId;
        
        SSAOFeature::PreparedInputs ssaoInputs;
        // GBuffer RG handles will be passed in appendGraph
        
        auto ssaoPrepared = SSAOFeature{}.prepareBindings(resolvedView.ssao, ssaoInputs, ssaoCtx);
        
        if (ssaoPrepared.isErr()) {
            YA_CORE_ERROR("Failed to prepare SSAO bindings for view {}", resolvedView.viewId);
            return ssaoPrepared.error();
        }
        view.ssao = ssaoPrepared.value();
        
        // Prepare Lighting bindings
        view.lighting = LightingFeature::prepareBindings(
            nullptr, // DeferredFrameResourceSet - will be passed in C3
            LightingFeature::Facts{.viewId = resolvedView.viewId},
            resolvedView.lighting);
        
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
    
    // Setup feature graph context
    FeatureGraphContext ctx;
    ctx.viewLease = nullptr; // Will be obtained from ViewTargetStore
    
    // Get view target lease
    ViewTargetLease lease;
    if (viewTargets) {
        lease = viewTargets->lease(resolved.viewId);
    }
    
    if (!lease) {
        YA_CORE_ERROR("Failed to get view target lease for view {}", resolved.viewId);
        return FeatureError::AllocationFailed;
    }
    ctx.viewLease = &lease;
    
    // 1. Append GBufferFeature
    GBufferFeature gbufferFeature;
    outputs.gbuffer = gbufferFeature.append(
        graph,
        nullptr, // GBufferStage - will be passed in C3
        ctx,
        GBufferFeature::Facts{
            .viewId = resolved.viewId,
            .viewExtent = Extent2D{800, 600}, // TODO: get from resolved
            .frameData = stageCtx.frameData,
            .flightIndex = stageCtx.flightIndex,
        },
        resolved.gbuffer,
        prepared.gbuffer,
        GBufferFeature::Inputs{});
    
    // 2. Append SSAOFeature
    SSAOFeature::PreparedInputs ssaoInputs;
    ssaoInputs.frameBuffer = RGBufferHandle{}; // TODO: Upload frame buffer
    ssaoInputs.albedo = outputs.gbuffer.albedo;
    ssaoInputs.normal = outputs.gbuffer.normal;
    ssaoInputs.depth = outputs.gbuffer.depth;
    
    SSAOFeature::FeatureGraphContext ssaoCtx;
    ssaoCtx.graph = &graph;
    ssaoCtx.stageCtx = &stageCtx;
    ssaoCtx.viewTargets = viewTargets;
    
    auto ssaoOutputs = SSAOFeature{}.append(resolved.ssao, prepared.ssao, ssaoInputs, ssaoCtx);
    
    if (ssaoOutputs.isErr()) {
        YA_CORE_ERROR("Failed to append SSAO to graph for view {}", resolved.viewId);
        return ssaoOutputs.error();
    }
    outputs.ao = ssaoOutputs.value().ao;
    
    // 3. Append LightingFeature
    LightingFeature::Inputs lightingInputs;
    lightingInputs.albedo = outputs.gbuffer.albedo;
    lightingInputs.normal = outputs.gbuffer.normal;
    lightingInputs.orm = outputs.gbuffer.orm;
    lightingInputs.shading = outputs.gbuffer.shading;
    lightingInputs.depth = outputs.gbuffer.depth;
    
    // Convert OptionalAO to std::optional<RGTextureHandle>
    if (outputs.ao.isValid()) {
        lightingInputs.ssao = outputs.ao.get();
    }
    
    LightingFeature lightingFeature;
    outputs.lighting = lightingFeature.append(
        graph,
        nullptr, // LightStage - will be passed in C3
        ctx,
        LightingFeature::Facts{
            .viewId = resolved.viewId,
            .viewExtent = Extent2D{800, 600}, // TODO: get from resolved
            .bReverseY = true,
        },
        resolved.lighting,
        prepared.lighting,
        lightingInputs);
    
    return outputs;
}

} // namespace ya
