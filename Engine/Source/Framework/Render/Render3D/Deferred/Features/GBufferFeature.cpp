#include "Render3D/Deferred/Features/GBufferFeature.h"

#include "Render3D/Deferred/GBufferStage.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Graph/RenderGraph.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Core/Log.h"

namespace ya
{

GBufferFeature::Resolved GBufferFeature::resolve(
    const Settings& settings,
    const Facts& facts,
    const Inputs& inputs)
{
    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    
    // GBuffer attachment roles are fixed (GBuffer0-3 + SceneDepth)
    resolved.outputs.albedoRole  = EViewAttachment::GBuffer0;
    resolved.outputs.normalRole  = EViewAttachment::GBuffer1;
    resolved.outputs.ormRole     = EViewAttachment::GBuffer2;
    resolved.outputs.shadingRole = EViewAttachment::GBuffer3;
    resolved.outputs.depthRole   = EViewAttachment::SceneDepth;
    
    return resolved;
}

GBufferFeature::Prepared GBufferFeature::prepareBindings(
    DeferredFrameResourceSet* resourceSet,
    const Facts& facts,
    const Resolved& resolved)
{
    Prepared prepared;
    
    if (!resolved.bEnabled || !resourceSet) {
        return prepared;
    }
    
    // Note: Actual binding allocation happens in append() when we have RenderSubmission
    // This is just a placeholder for the protocol
    
    return prepared;
}

GBufferFeature::GraphOutputs GBufferFeature::append(
    RenderGraph& graph,
    GBufferStage* stage,
    const FeatureGraphContext& ctx,
    const Facts& facts,
    const Resolved& resolved,
    const Prepared& prepared,
    const Inputs& inputs)
{
    GraphOutputs outputs;
    
    if (!resolved.bEnabled || !stage || !ctx.viewLease) {
        return outputs;
    }
    
    // Helper to import texture from ViewTargetLease
    const auto importTarget = [&](EViewAttachment role, std::string_view label, EImageLayout::T finalLayout, EImageUsage::T usage) {
        auto target = ctx.viewLease->find(role);
        YA_CORE_ASSERT(target != nullptr, "GBuffer target missing role {}", static_cast<uint32_t>(role));
        return graph.importTexture(makeImportedTextureDesc(*target, label, finalLayout, usage));
    };
    
    // Import GBuffer attachments
    outputs.albedo = importTarget(
        resolved.outputs.albedoRole,
        "GBuffer.Albedo",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled);
    
    outputs.normal = importTarget(
        resolved.outputs.normalRole,
        "GBuffer.Normal",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled);
    
    outputs.orm = importTarget(
        resolved.outputs.ormRole,
        "GBuffer.ORM",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled);
    
    outputs.shading = importTarget(
        resolved.outputs.shadingRole,
        "GBuffer.Shading",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled);
    
    outputs.depth = importTarget(
        resolved.outputs.depthRole,
        "GBuffer.Depth",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::DepthStencilAttachment | EImageUsage::Sampled);
    
    // Add GBuffer pass
    Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    
    graph.addPass("GBuffer", [&](RGPassBuilder& builder) {
        builder.declareRaster({
            .renderArea = renderArea,
            .layerCount = 1,
            .colors = {
                {.color = outputs.albedo,  .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                {.color = outputs.normal,  .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                {.color = outputs.orm,     .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                {.color = outputs.shading, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
            },
            .depth = RGDepthAttachmentDesc{
                .depth       = outputs.depth,
                .clearValue  = ClearValue(1.0f, 0),
                .loadOp      = EAttachmentLoadOp::Clear,
                .storeOp     = EAttachmentStoreOp::Store,
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            },
        });
    }, [=](RGRenderContext& rgCtx) {
        // Execute GBufferStage
        // Note: This is a placeholder - full integration needs frame/light bindings
        // which will be provided when we wire this into DeferredRenderPipeline
        rgCtx.beginDeclaredRasterRendering();
        
        // Actual GBufferStage::execute() call will go here with proper bindings
    });
    
    return outputs;
}

} // namespace ya
