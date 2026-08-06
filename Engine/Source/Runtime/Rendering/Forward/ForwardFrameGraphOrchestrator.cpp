#include "ForwardFrameGraphOrchestrator.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/EntityIdViewportPass.h"
#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Forward/ForwardViewportStage.h"

#include <string_view>

namespace ya
{

namespace
{

constexpr std::string_view kForwardTopologyPassShadow          = "Shadow Subgraph";
constexpr std::string_view kForwardTopologyPassOpaque          = "Forward Opaque";
constexpr std::string_view kForwardTopologyPassSkybox          = "Forward Skybox";
constexpr std::string_view kForwardTopologyPassTransparent     = "Forward Transparent";
constexpr std::string_view kForwardTopologyPassOverlay         = "Forward Overlay";
constexpr std::string_view kForwardTopologyPassBloom           = "Bloom Subgraph";
constexpr std::string_view kForwardTopologyPassPostprocessing  = "Postprocessing";
RGTextureDesc makeForwardViewportTextureDesc(const AttachmentDescription& attachment,
                                             Extent2D                    extent,
                                             uint32_t                    layerCount,
                                             std::string                  label)
{
    return RGTextureDesc{
        .label       = std::move(label),
        .format      = attachment.format,
        .extent      = Extent3D{extent.width, extent.height, 1},
        .mipLevels   = 1,
        .arrayLayers = layerCount,
        .samples     = attachment.samples,
        .usage       = attachment.usage,
        .flags       = attachment.imageCreateFlags,
    };
}

struct ForwardViewportGraphResources
{
    RGTextureHandle         color{};
    RGTextureHandle         resolve{};
    RGTextureHandle         depth{};
    RGTextureHandle         entityId{};
    std::optional<RGTextureHandle> shadowDepth{};
    Extent2D                viewportExtent{};
    AttachmentDescription   colorAttachment{};
    AttachmentDescription   depthAttachment{};
    AttachmentDescription   entityIdAttachment{};
    Rect2D                  renderArea{};
};

struct ForwardViewportPassBundle
{
    ForwardOpaquePassParams      opaque{};
    ForwardSkyboxPassParams      skybox{};
    ForwardTransparentPassParams transparent{};
    ForwardEntityIdPassParams    entityId{};
    ForwardOverlayPassParams     overlay{};
};

AttachmentDescription makeEntityIdAttachmentDesc()
{
    return AttachmentDescription{
        .format   = EFormat::R32_UINT,
        .samples  = ESampleCount::Sample_1,
        .loadOp   = EAttachmentLoadOp::Clear,
        .storeOp  = EAttachmentStoreOp::Store,
        .usage    = EImageUsage::ColorAttachment | EImageUsage::TransferSrc,
        .finalLayout = EImageLayout::ColorAttachmentOptimal,
    };
}

ForwardViewportGraphResources createForwardViewportResources(RenderGraph&                   graph,
                                                             const RenderTargetCreateInfo& viewportRTSpec,
                                                             const ShadowGraphOutputs&      shadowOutputs)
{
    const auto colorAttachment = viewportRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *viewportRTSpec.attachments.depthAttach;
    const uint32_t layerCount = viewportRTSpec.layerCount;
    const auto color = graph.createPersistentTexture(
        makeForwardViewportTextureDesc(colorAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.Color"),
        RGPersistentTextureKey{.value = "ForwardViewport.Color"});
    const RGTextureHandle resolve = viewportRTSpec.attachments.resolveAttach.has_value()
        ? graph.createPersistentTexture(
              makeForwardViewportTextureDesc(
                  *viewportRTSpec.attachments.resolveAttach,
                  viewportRTSpec.extent,
                  layerCount,
                  "ForwardViewport.Resolve"),
              RGPersistentTextureKey{.value = "ForwardViewport.Resolve"})
        : RGTextureHandle{};
    const auto depth = graph.createPersistentTexture(
        makeForwardViewportTextureDesc(depthAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.Depth"),
        RGPersistentTextureKey{.value = "ForwardViewport.Depth"});
    const auto entityIdAttachment = makeEntityIdAttachmentDesc();
    const auto entityId = graph.createPersistentTexture(
        makeForwardViewportTextureDesc(entityIdAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.EntityId"),
        RGPersistentTextureKey{.value = "ForwardViewport.EntityId"});

    return ForwardViewportGraphResources{
        .color           = color,
        .resolve         = resolve,
        .depth           = depth,
        .entityId        = entityId,
        .shadowDepth     = shadowOutputs.shadowDepth,
        .viewportExtent  = viewportRTSpec.extent,
        .colorAttachment = colorAttachment,
        .depthAttachment = depthAttachment,
        .entityIdAttachment = entityIdAttachment,
        .renderArea      = Rect2D{.pos = {0, 0}, .extent = viewportRTSpec.extent.toVec2()},
    };
}

ForwardViewportPassBundle buildForwardViewportPassBundle(const ForwardFrameGraphOrchestrator::BuildInputs& inputs,
                                                         const ForwardViewportGraphResources&               resources)
{
    return ForwardViewportPassBundle{
        .opaque = {
            .viewportColor   = resources.color,
            .viewportDepth   = resources.depth,
            .renderArea      = resources.renderArea,
            .layerCount      = 1,
            .finalLayout     = EImageLayout::ColorAttachmentOptimal,
            .directionGizmos = inputs.directionGizmos,
        },
        .skybox = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .transparent = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .entityId = {
            .viewportColor = resources.entityId,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .overlay = {
            .viewportColor   = resources.color,
            .viewportDepth   = resources.depth,
            .renderArea      = resources.renderArea,
            .layerCount      = 1,
            .finalLayout     = resources.colorAttachment.finalLayout,
            .overlaySnapshot = inputs.viewportOverlaySnapshot,
            .frameCtx        = inputs.postContext ? *inputs.postContext : FrameContext{},
        },
    };
}

void appendForwardViewportPasses(RenderGraph&                                         graph,
                                 ForwardViewportStage&                                viewportStage,
                                 EntityIdViewportPass&                                entityIdPass,
                                 RenderStageContext&                                  stageCtx,
                                 const ForwardFrameResourceSet::Binding&              frameBinding,
                                 ForwardViewportStage::PassContext*                   passContext,
                                    const ForwardViewportGraphResources&                 resources,
                                 std::optional<RGTextureHandle>                       shadowDepth,
                                 ForwardViewportPassBundle                            params)
{
    [[maybe_unused]] const auto opaquePass = graph.addPass(
        std::string(kForwardTopologyPassOpaque),
        [&opaqueParams = params.opaque, shadowDepth, colorAttachment = resources.colorAttachment, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = opaqueParams.renderArea,
                .layerCount = opaqueParams.layerCount,
                .colors = {{
                    .color       = opaqueParams.viewportColor,
                    .clearValue  = ClearValue::Black(),
                    .loadOp      = colorAttachment.loadOp,
                    .storeOp     = colorAttachment.storeOp,
                    .finalLayout = opaqueParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = opaqueParams.viewportDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = depthAttachment.loadOp,
                    .storeOp     = depthAttachment.storeOp,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&viewportStage, &stageCtx, frameBinding, directionGizmos = std::move(params.opaque.directionGizmos), passContext](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executePBR(stageCtx, frameBinding, passContext);
            viewportStage.executePhong(stageCtx, frameBinding, passContext);
            viewportStage.executeUnlit(stageCtx, frameBinding, passContext);
            viewportStage.executeSimple(stageCtx, passContext);
            viewportStage.executeDirection(stageCtx, std::move(directionGizmos), passContext);
            viewportStage.executeDebug(stageCtx, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto skyboxPass = graph.addPass(
        std::string(kForwardTopologyPassSkybox),
        [&skyboxParams = params.skybox, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = skyboxParams.renderArea,
                .layerCount = skyboxParams.layerCount,
                .colors = {{
                    .color       = skyboxParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = skyboxParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = skyboxParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&viewportStage, &stageCtx, frameBinding, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeSkybox(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto transparentPass = graph.addPass(
        std::string(kForwardTopologyPassTransparent),
        [&transparentParams = params.transparent, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = transparentParams.renderArea,
                .layerCount = transparentParams.layerCount,
                .colors = {{
                    .color       = transparentParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = transparentParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = transparentParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&stageCtx](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            rgCtx.endRendering();
        });

    // Entity-id pick pass: writes every draw item's entity id into an R32_UINT
    // target, depth-tested against the viewport depth so ids match what is
    // visible. A cursor readback of this target yields the picked entity.
    [[maybe_unused]] const auto entityIdPassHandle = graph.addPass(
        std::string("Forward EntityId"),
        [&entityIdParams = params.entityId, entityIdAttachment = resources.entityIdAttachment, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = entityIdParams.renderArea,
                .layerCount = entityIdParams.layerCount,
                .colors = {{
                    .color       = entityIdParams.viewportColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = entityIdAttachment.loadOp,
                    .storeOp     = entityIdAttachment.storeOp,
                    .finalLayout = entityIdParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = entityIdParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&entityIdPass, &stageCtx, frameBinding](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            if (stageCtx.frameData) {
                entityIdPass.execute(&rgCtx.getCommandBuffer(),
                                     viewportExtent.width,
                                     viewportExtent.height,
                                      stageCtx.frameData->projection * stageCtx.frameData->view,
                                      stageCtx.frameData->view,
                                      *stageCtx.frameData,
                                      frameBinding.skinningDescriptorSet);
            }
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto overlayPass = graph.addPass(
        std::string(kForwardTopologyPassOverlay),
        [&overlayParams = params.overlay, resolve = resources.resolve, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = overlayParams.renderArea,
                .layerCount = overlayParams.layerCount,
                .colors = {{
                    .color       = overlayParams.viewportColor,
                    .resolve     = resolve,
                    .resolveMode = resolve.isValid() ? EResolveMode::Average : EResolveMode::None,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = overlayParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = overlayParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&stageCtx, overlayParams = params.overlay](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            overlayParams.frameCtx.extent = viewportExtent;
            recordRenderViewportOverlayPass(overlayParams.frameCtx,
                                            overlayParams.overlaySnapshot,
                                            &rgCtx.getCommandBuffer());
            rgCtx.endRendering();
        });
}

void appendForwardPostprocessPasses(RenderGraph&                 graph,
                                    PostProcessingStage&         postProcessStage,
                                    const ForwardViewportGraphResources& resources,
                                    FrameContext*                postContext,
                                    bool                         bOutputIsSRGB)
{
    const auto postprocessInput = resources.resolve.isValid() ? resources.resolve : resources.color;
    const auto bloomComposite   = postProcessStage.appendBloomGraphPasses(graph, postprocessInput, resources.viewportExtent, postContext);
    const auto finalizeInput    = bloomComposite.isValid() ? bloomComposite : postprocessInput;
    [[maybe_unused]] const auto postprocessOutput = postProcessStage.appendFinalizeGraphPasses(graph, PostProcessingStage::FinalizePassParams{
        .input         = finalizeInput,
        .inputExtent   = resources.viewportExtent,
        .bOutputIsSRGB = bOutputIsSRGB,
        .postContext   = postContext,
    });
}

} // namespace

void ForwardFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
{
    YA_CORE_ASSERT(inputs.graph != nullptr, "ForwardFrameGraphOrchestrator requires a render graph");
    YA_CORE_ASSERT(inputs.stageCtx != nullptr, "ForwardFrameGraphOrchestrator requires a stage context");
    YA_CORE_ASSERT(inputs.viewportRTSpec != nullptr, "ForwardFrameGraphOrchestrator requires a viewport render target spec");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.viewportStage != nullptr, "ForwardFrameGraphOrchestrator requires a viewport stage");
    YA_CORE_ASSERT(deps.entityIdPass != nullptr, "ForwardFrameGraphOrchestrator requires an entity-id pass");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess stage");

    auto&       graph        = *inputs.graph;
    auto&       stageCtx     = *inputs.stageCtx;
    const auto  frameBinding      = inputs.frameBinding;
    const auto& viewportRTSpec    = *inputs.viewportRTSpec;

    ShadowGraphOutputs shadowOutputs;
    if (deps.shadowStage && inputs.bEnableShadow) {
        shadowOutputs = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }

    const auto graphResources = createForwardViewportResources(graph, viewportRTSpec, shadowOutputs);
    auto       viewportPasses = buildForwardViewportPassBundle(inputs, graphResources);

    appendForwardViewportPasses(graph,
                                *deps.viewportStage,
                                *deps.entityIdPass,
                                stageCtx,
                                frameBinding,
                                inputs.viewportPassContext,
                                graphResources,
                                graphResources.shadowDepth,
                                std::move(viewportPasses));
    appendForwardPostprocessPasses(
        graph,
        *deps.postProcessStage,
        graphResources,
        inputs.postContext,
        inputs.bPostprocessOutputIsSRGB);

    graph.exportTexture(graphResources.color, std::string(forward_graph_exports::viewportColor));
    graph.exportTexture(graphResources.depth, std::string(forward_graph_exports::viewportDepth));
    if (graphResources.resolve.isValid()) {
        graph.exportTexture(graphResources.resolve, std::string(forward_graph_exports::viewportResolve));
    }
    graph.exportTexture(graphResources.entityId, std::string(forward_graph_exports::entityId));
}

} // namespace ya
