#include "ForwardFrameGraphOrchestrator.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Forward/ForwardViewportStage.h"

#include <string_view>

namespace ya
{

namespace
{

constexpr std::string_view kForwardTopologyPassShadow          = "Shadow Subgraph";
constexpr std::string_view kForwardTopologyPassSkybox          = "Forward Skybox";
constexpr std::string_view kForwardTopologyPassPBR             = "Forward PBR";
constexpr std::string_view kForwardTopologyPassPhong           = "Forward Phong";
constexpr std::string_view kForwardTopologyPassUnlit           = "Forward Unlit";
constexpr std::string_view kForwardTopologyPassSimple          = "Forward Simple";
constexpr std::string_view kForwardTopologyPassDirection       = "Forward Direction";
constexpr std::string_view kForwardTopologyPassDebug           = "Forward Debug";
constexpr std::string_view kForwardTopologyPassViewportOverlay = "Forward Viewport Overlay";
constexpr std::string_view kForwardTopologyPassBloom           = "Bloom Subgraph";
constexpr std::string_view kForwardTopologyPassPostprocessing  = "Postprocessing";

RGImportedTextureDesc makeForwardViewportImportedDesc(const RenderImage& image,
                                                      std::string_view   label,
                                                      EImageLayout::T    finalLayout)
{
    return makeImportedTextureDesc(image, label, finalLayout);
}

} // namespace

void ForwardFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
{
    YA_CORE_ASSERT(inputs.graph != nullptr, "ForwardFrameGraphOrchestrator requires a render graph");
    YA_CORE_ASSERT(inputs.stageCtx != nullptr, "ForwardFrameGraphOrchestrator requires a stage context");
    YA_CORE_ASSERT(inputs.viewportRTSpec != nullptr, "ForwardFrameGraphOrchestrator requires a viewport render target spec");
    YA_CORE_ASSERT(inputs.viewportResources != nullptr, "ForwardFrameGraphOrchestrator requires viewport resources");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.viewportStage != nullptr, "ForwardFrameGraphOrchestrator requires a viewport stage");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess stage");

    auto&       graph        = *inputs.graph;
    auto&       stageCtx     = *inputs.stageCtx;
    const auto  frameBinding = inputs.frameBinding;
    const auto& viewportResources = *inputs.viewportResources;
    const auto& viewportRTSpec    = *inputs.viewportRTSpec;

    ShadowGraphOutputs shadowOutputs;
    if (deps.shadowStage && inputs.bEnableShadow) {
        shadowOutputs = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }

    const auto color = graph.importTexture(
        makeForwardViewportImportedDesc(*viewportResources.colorImage,
                                        "ForwardViewport.Color",
                                        viewportRTSpec.attachments.colorAttach[0].finalLayout));
    const RGTextureHandle resolve = viewportResources.resolveImage
        ? graph.importTexture(
              makeForwardViewportImportedDesc(*viewportResources.resolveImage,
                                              "ForwardViewport.Resolve",
                                              viewportRTSpec.attachments.colorAttach[0].finalLayout))
        : RGTextureHandle{};
    const auto depth = graph.importTexture(
        makeForwardViewportImportedDesc(*viewportResources.depthImage,
                                        "ForwardViewport.Depth",
                                        viewportRTSpec.attachments.depthAttach->finalLayout));
    const auto shadowDepth    = shadowOutputs.shadowDepth;
    const auto viewportExtent = viewportResources.extent;
    const auto colorAttachment = viewportRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *viewportRTSpec.attachments.depthAttach;
    const Rect2D renderArea{.pos = {0, 0}, .extent = viewportExtent.toVec2()};
    // FG-702~704: the Forward viewport sequence is declared as separate graph
    // passes (Skybox -> PBR -> Phong -> Unlit -> Simple -> Direction -> Debug
    // -> Viewport Overlay). The stage exposes one entry per pass; the graph
    // owns the order and the attachment lifetimes. Skybox is the first pass
    // and clears the viewport; Viewport Overlay is the last pass and owns the
    // MSAA resolve attachment plus the editor viewport overlays. The
    // attachment chain stays in attachment-optimal layout between passes; only
    // Viewport Overlay applies the final consumer layout
    // (`EImageLayout::ShaderReadOnlyOptimal`, matching the imported final
    // layout used by postprocess inside the same graph).
    ForwardSkyboxPassParams skyboxParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardPBRPassParams pbrParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardPhongPassParams phongParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardUnlitPassParams unlitParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardSimplePassParams simpleParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardDirectionPassParams directionParams{
        .viewportColor   = color,
        .viewportDepth   = depth,
        .renderArea      = renderArea,
        .layerCount      = 1,
        .finalLayout     = EImageLayout::ColorAttachmentOptimal,
        .directionGizmos = inputs.directionGizmos,
    };
    ForwardDebugPassParams debugParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardViewportOverlayPassParams viewportOverlayParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = colorAttachment.finalLayout,
        .recordViewportOverlays = inputs.recordViewportOverlays,
    };

    [[maybe_unused]] const auto skyboxPass = graph.addPass(
        std::string(kForwardTopologyPassSkybox),
        [&skyboxParams, colorAttachment, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = skyboxParams.renderArea,
                .layerCount = skyboxParams.layerCount,
                .colors = {{
                    .color       = skyboxParams.viewportColor,
                    .clearValue  = ClearValue::Black(),
                    .loadOp      = colorAttachment.loadOp,
                    .storeOp     = colorAttachment.storeOp,
                    .finalLayout = skyboxParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = skyboxParams.viewportDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = depthAttachment.loadOp,
                    .storeOp     = depthAttachment.storeOp,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, frameBinding, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executeSkybox(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto pbrPass = graph.addPass(
        std::string(kForwardTopologyPassPBR),
        [&pbrParams, shadowDepth, depthAttachment](RGPassBuilder& passBuilder) {
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = pbrParams.renderArea,
                .layerCount = pbrParams.layerCount,
                .colors = {{
                    .color       = pbrParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = pbrParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = pbrParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, frameBinding, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executePBR(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto phongPass = graph.addPass(
        std::string(kForwardTopologyPassPhong),
        [&phongParams, shadowDepth, depthAttachment](RGPassBuilder& passBuilder) {
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = phongParams.renderArea,
                .layerCount = phongParams.layerCount,
                .colors = {{
                    .color       = phongParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = phongParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = phongParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, frameBinding, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executePhong(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto unlitPass = graph.addPass(
        std::string(kForwardTopologyPassUnlit),
        [&unlitParams, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = unlitParams.renderArea,
                .layerCount = unlitParams.layerCount,
                .colors = {{
                    .color       = unlitParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = unlitParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = unlitParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, frameBinding, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executeUnlit(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto simplePass = graph.addPass(
        std::string(kForwardTopologyPassSimple),
        [&simpleParams, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = simpleParams.renderArea,
                .layerCount = simpleParams.layerCount,
                .colors = {{
                    .color       = simpleParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = simpleParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = simpleParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executeSimple(stageCtx, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto directionPass = graph.addPass(
        std::string(kForwardTopologyPassDirection),
        [&directionParams, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = directionParams.renderArea,
                .layerCount = directionParams.layerCount,
                .colors = {{
                    .color       = directionParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = directionParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = directionParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, directionParams, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executeDirection(stageCtx, std::move(directionParams.directionGizmos), passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto debugPass = graph.addPass(
        std::string(kForwardTopologyPassDebug),
        [&debugParams, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = debugParams.renderArea,
                .layerCount = debugParams.layerCount,
                .colors = {{
                    .color       = debugParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = debugParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = debugParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [viewportStage = deps.viewportStage, &stageCtx, passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            viewportStage->executeDebug(stageCtx, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto viewportOverlayPass = graph.addPass(
        std::string(kForwardTopologyPassViewportOverlay),
        [&viewportOverlayParams, resolve, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = viewportOverlayParams.renderArea,
                .layerCount = viewportOverlayParams.layerCount,
                .colors = {{
                    .color       = viewportOverlayParams.viewportColor,
                    .resolve     = resolve,
                    .resolveMode = resolve.isValid() ? EResolveMode::Average : EResolveMode::None,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = viewportOverlayParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = viewportOverlayParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [&stageCtx, viewportOverlayParams](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            if (viewportOverlayParams.recordViewportOverlays) {
                viewportOverlayParams.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent);
            }
            rgCtx.endRendering();
        });
    // FG-705: bloom + finalize stay inside the same graph. The postprocess
    // input is the resolved viewport (the MSAA resolve target when present);
    // the finalize pass creates its output texture and exports it under
    // PostProcessingStage::kOutputExportName.
    const auto postprocessInput = resolve.isValid() ? resolve : color;
    const auto bloomComposite   = deps.postProcessStage->appendBloomGraphPasses(graph, postprocessInput, viewportExtent, inputs.postContext);
    const auto finalizeInput    = bloomComposite.isValid() ? bloomComposite : postprocessInput;
    [[maybe_unused]] const auto postprocessOutput = deps.postProcessStage->appendFinalizeGraphPasses(graph, PostProcessingStage::FinalizePassParams{
        .input         = finalizeInput,
        .inputExtent   = viewportExtent,
        .bOutputIsSRGB = inputs.bPostprocessOutputIsSRGB,
        .postContext   = inputs.postContext,
    });
}

} // namespace ya
