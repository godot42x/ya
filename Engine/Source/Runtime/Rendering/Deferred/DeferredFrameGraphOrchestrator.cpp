#include "DeferredFrameGraphOrchestrator.h"

#include "Core/Profiling/Profiling.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Deferred/DeferredFrameGraphPasses.h"

namespace ya
{

void DeferredFrameGraphOrchestrator::build(
    const BuildDependencies& deps,
    const BuildInputs& inputs) const
{
    YA_PROFILE_FUNCTION();
    YA_CORE_ASSERT(inputs.graph != nullptr, "DeferredFrameGraphOrchestrator requires a render graph");
    YA_CORE_ASSERT(inputs.graphResources != nullptr, "DeferredFrameGraphOrchestrator requires graph resources");
    YA_CORE_ASSERT(inputs.stageCtx != nullptr, "DeferredFrameGraphOrchestrator requires a stage context");
    YA_CORE_ASSERT(inputs.frameBinding != nullptr, "DeferredFrameGraphOrchestrator requires frame bindings");
    YA_CORE_ASSERT(inputs.gBufferRTSpec != nullptr, "DeferredFrameGraphOrchestrator requires a GBuffer render target spec");
    YA_CORE_ASSERT(inputs.viewportRTSpec != nullptr, "DeferredFrameGraphOrchestrator requires a viewport render target spec");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "DeferredFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.gBufferStage != nullptr, "DeferredFrameGraphOrchestrator requires a GBuffer stage");
    YA_CORE_ASSERT(deps.lightStage != nullptr, "DeferredFrameGraphOrchestrator requires a light stage");
    YA_CORE_ASSERT(deps.overlayStage != nullptr, "DeferredFrameGraphOrchestrator requires an overlay stage");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "DeferredFrameGraphOrchestrator requires a postprocess stage");

    auto&       graph          = *inputs.graph;
    auto&       graphResources = *inputs.graphResources;
    const auto& stageCtx       = *inputs.stageCtx;

    if (deps.shadowStage) {
        graphResources.passes.shadow = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }

    DeferredFrameGraphPassContext context{
        .graph                    = *inputs.graph,
        .graphResources           = *inputs.graphResources,
        .stageCtx                 = *inputs.stageCtx,
        .frameBinding             = *inputs.frameBinding,
        .gBufferRTSpec            = *inputs.gBufferRTSpec,
        .viewportRTSpec           = *inputs.viewportRTSpec,
        .overlayInputs            = inputs.overlayInputs,
        .environmentLighting      = inputs.environmentLighting,
        .environmentLightingDS    = inputs.environmentLightingDS,
        .postContext              = inputs.postContext,
        .viewportExtent           = inputs.viewportExtent,
        .bUseSSAO                 = inputs.bUseSSAO,
        .bReverseViewportY        = inputs.bReverseViewportY,
        .bPostprocessOutputIsSRGB = inputs.bPostprocessOutputIsSRGB,
        .viewportOverlaySnapshot  = inputs.viewportOverlaySnapshot,
        .shadowStage              = deps.shadowStage,
        .gBufferStage             = *deps.gBufferStage,
        .lightStage               = *deps.lightStage,
        .overlayStage             = *deps.overlayStage,
        .postProcessStage         = *deps.postProcessStage,
        .ssaoStage                = deps.ssaoStage,
        .entityIdPass             = deps.entityIdPass,
    };

    deferred_frame_graph_passes::importFrameBuffers(context);
    deferred_frame_graph_passes::createAttachmentTextures(context);
    deferred_frame_graph_passes::appendGBuffer(context);
    deferred_frame_graph_passes::appendSSAO(context);
    deferred_frame_graph_passes::appendLight(context);
    deferred_frame_graph_passes::appendForwardOpaque(context);
    deferred_frame_graph_passes::appendSkybox(context);
    deferred_frame_graph_passes::appendBloom(context);
    deferred_frame_graph_passes::appendForwardTransparent(context);
    deferred_frame_graph_passes::appendEntityId(context);
    deferred_frame_graph_passes::appendOverlay(context);
    deferred_frame_graph_passes::appendPostprocess(context);

    exportGraphOutputs(graph, graphResources);
}

void DeferredFrameGraphOrchestrator::exportGraphOutputs(
    RenderGraph& graph,
    const DeferredFrameGraphResources& resources) const
{
    for (uint32_t attachmentIndex = 0; attachmentIndex < resources.textures.gBufferColors.size(); ++attachmentIndex) {
        graph.exportTexture(
            resources.textures.gBufferColors[attachmentIndex],
            std::string(deferred_graph_exports::gBufferColor[attachmentIndex]));
    }
    graph.exportTexture(resources.textures.gBufferDepth, std::string(deferred_graph_exports::gBufferDepth));
    graph.exportTexture(resources.textures.viewportColor, std::string(deferred_graph_exports::viewportColor));
    graph.exportTexture(resources.textures.entityId, std::string(deferred_graph_exports::entityId));
    if (resources.textures.ssao.has_value()) {
        graph.exportTexture(*resources.textures.ssao, std::string(deferred_graph_exports::ssao));
    }
}

} // namespace ya
