#include "Render3D/Deferred/Features/LightingFeature.h"

#include "Render3D/Deferred/LightStage.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Graph/RenderGraph.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Core/Log.h"

namespace ya
{

LightingFeature::Resolved LightingFeature::resolve(
    const Settings& settings,
    const Facts& facts,
    const Inputs& inputs)
{
    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    
    // SceneColor attachment
    resolved.outputs.sceneColorRole = EViewAttachment::SceneColor;
    resolved.outputs.sceneColorFormat = EFormat::R16G16B16A16_SFLOAT;
    resolved.outputs.sceneColorUsage = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    
    return resolved;
}

LightingFeature::Prepared LightingFeature::prepareBindings(
    DeferredFrameResourceSet* resourceSet,
    const Facts& facts,
    const Resolved& resolved)
{
    Prepared prepared;
    
    if (!resolved.bEnabled || !resourceSet) {
        return prepared;
    }
    
    // Descriptor allocation happens in append() with full context
    
    return prepared;
}

LightingFeature::GraphOutputs LightingFeature::append(
    RenderGraph& graph,
    LightStage* stage,
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
    
    // Import SceneColor target
    auto sceneColorTarget = ctx.viewLease->find(resolved.outputs.sceneColorRole);
    YA_CORE_ASSERT(sceneColorTarget != nullptr, "Lighting SceneColor target missing");
    
    outputs.sceneColor = graph.importTexture(makeImportedTextureDesc(
        *sceneColorTarget,
        "Lighting.SceneColor",
        EImageLayout::ShaderReadOnlyOptimal,
        resolved.outputs.sceneColorUsage));
    
    // Add lighting pass
    Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    
    graph.addPass("Lighting", [&](RGPassBuilder& builder) {
        // Read GBuffer textures
        builder.read(inputs.albedo);
        builder.read(inputs.normal);
        builder.read(inputs.orm);
        builder.read(inputs.shading);
        builder.read(inputs.depth);
        
        // Read optional SSAO
        if (inputs.ssao.has_value()) {
            builder.read(*inputs.ssao);
        }
        
        // Write SceneColor
        builder.declareRaster({
            .renderArea = renderArea,
            .layerCount = 1,
            .colors = {
                {.color = outputs.sceneColor, .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
            },
        });
    }, [=](RGRenderContext& rgCtx) {
        // Execute LightStage
        // Note: Full integration needs frame/light/environment/shadow bindings
        // which will be provided when wired into DeferredRenderPipeline
        rgCtx.beginDeclaredRasterRendering();
        
        // Actual LightStage::execute() call will go here with proper descriptor sets
    });
    
    return outputs;
}

} // namespace ya
