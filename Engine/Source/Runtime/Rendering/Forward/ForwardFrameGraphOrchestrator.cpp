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

struct ForwardImportedViewportResources
{
    RGTextureHandle         color{};
    RGTextureHandle         resolve{};
    RGTextureHandle         depth{};
    std::optional<RGTextureHandle> shadowDepth{};
    Extent2D                viewportExtent{};
    AttachmentDescription   colorAttachment{};
    AttachmentDescription   depthAttachment{};
    Rect2D                  renderArea{};
};

struct ForwardViewportPassBundle
{
    ForwardSkyboxPassParams          skybox{};
    ForwardPBRPassParams             pbr{};
    ForwardPhongPassParams           phong{};
    ForwardUnlitPassParams           unlit{};
    ForwardSimplePassParams          simple{};
    ForwardDirectionPassParams       direction{};
    ForwardDebugPassParams           debug{};
    ForwardViewportOverlayPassParams viewportOverlay{};
};

ForwardImportedViewportResources importForwardViewportResources(RenderGraph&                               graph,
                                                               const ForwardViewportResources&            viewportResources,
                                                               const RenderTargetCreateInfo&             viewportRTSpec,
                                                               const ShadowGraphOutputs&                 shadowOutputs)
{
    const auto colorAttachment = viewportRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *viewportRTSpec.attachments.depthAttach;
    const auto color = graph.importTexture(
        makeForwardViewportImportedDesc(*viewportResources.colorImage,
                                        "ForwardViewport.Color",
                                        colorAttachment.finalLayout));
    const RGTextureHandle resolve = viewportResources.resolveImage
        ? graph.importTexture(
              makeForwardViewportImportedDesc(*viewportResources.resolveImage,
                                              "ForwardViewport.Resolve",
                                              colorAttachment.finalLayout))
        : RGTextureHandle{};
    const auto depth = graph.importTexture(
        makeForwardViewportImportedDesc(*viewportResources.depthImage,
                                        "ForwardViewport.Depth",
                                        depthAttachment.finalLayout));

    return ForwardImportedViewportResources{
        .color           = color,
        .resolve         = resolve,
        .depth           = depth,
        .shadowDepth     = shadowOutputs.shadowDepth,
        .viewportExtent  = viewportResources.extent,
        .colorAttachment = colorAttachment,
        .depthAttachment = depthAttachment,
        .renderArea      = Rect2D{.pos = {0, 0}, .extent = viewportResources.extent.toVec2()},
    };
}

ForwardViewportPassBundle buildForwardViewportPassBundle(const ForwardFrameGraphOrchestrator::BuildInputs& inputs,
                                                         const ForwardImportedViewportResources&            resources)
{
    return ForwardViewportPassBundle{
        .skybox = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .pbr = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .phong = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .unlit = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .simple = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .direction = {
            .viewportColor   = resources.color,
            .viewportDepth   = resources.depth,
            .renderArea      = resources.renderArea,
            .layerCount      = 1,
            .finalLayout     = EImageLayout::ColorAttachmentOptimal,
            .directionGizmos = inputs.directionGizmos,
        },
        .debug = {
            .viewportColor = resources.color,
            .viewportDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .viewportOverlay = {
            .viewportColor          = resources.color,
            .viewportDepth          = resources.depth,
            .renderArea             = resources.renderArea,
            .layerCount             = 1,
            .finalLayout            = resources.colorAttachment.finalLayout,
            .recordViewportOverlays = inputs.recordViewportOverlays,
        },
    };
}

void appendForwardViewportPasses(RenderGraph&                                         graph,
                                 ForwardViewportStage&                                viewportStage,
                                 RenderStageContext&                                  stageCtx,
                                 const ForwardFrameResourceSet::Binding&              frameBinding,
                                 ForwardViewportStage::PassContext*                   passContext,
                                 const ForwardImportedViewportResources&              resources,
                                 std::optional<RGTextureHandle>                       shadowDepth,
                                 ForwardViewportPassBundle                            params)
{
    [[maybe_unused]] const auto skyboxPass = graph.addPass(
        std::string(kForwardTopologyPassSkybox),
        [&skyboxParams = params.skybox, colorAttachment = resources.colorAttachment, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, frameBinding, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeSkybox(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto pbrPass = graph.addPass(
        std::string(kForwardTopologyPassPBR),
        [&pbrParams = params.pbr, shadowDepth, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, frameBinding, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executePBR(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto phongPass = graph.addPass(
        std::string(kForwardTopologyPassPhong),
        [&phongParams = params.phong, shadowDepth, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, frameBinding, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executePhong(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto unlitPass = graph.addPass(
        std::string(kForwardTopologyPassUnlit),
        [&unlitParams = params.unlit, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, frameBinding, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeUnlit(stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto simplePass = graph.addPass(
        std::string(kForwardTopologyPassSimple),
        [&simpleParams = params.simple, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeSimple(stageCtx, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto directionPass = graph.addPass(
        std::string(kForwardTopologyPassDirection),
        [&directionParams = params.direction, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, directionParams = std::move(params.direction), passContext](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeDirection(stageCtx, std::move(directionParams.directionGizmos), passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto debugPass = graph.addPass(
        std::string(kForwardTopologyPassDebug),
        [&debugParams = params.debug, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&viewportStage, &stageCtx, passContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            viewportStage.executeDebug(stageCtx, passContext);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto viewportOverlayPass = graph.addPass(
        std::string(kForwardTopologyPassViewportOverlay),
        [&viewportOverlayParams = params.viewportOverlay, resolve = resources.resolve, depthAttachment = resources.depthAttachment](RGPassBuilder& passBuilder) {
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
        [&stageCtx, viewportOverlayParams = params.viewportOverlay](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx.viewportExtent = viewportExtent;
            if (viewportOverlayParams.recordViewportOverlays) {
                viewportOverlayParams.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent);
            }
            rgCtx.endRendering();
        });
}

void appendForwardPostprocessPasses(RenderGraph&                 graph,
                                    PostProcessingStage&         postProcessStage,
                                    const ForwardImportedViewportResources& resources,
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
    YA_CORE_ASSERT(inputs.viewportResources != nullptr, "ForwardFrameGraphOrchestrator requires viewport resources");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.viewportStage != nullptr, "ForwardFrameGraphOrchestrator requires a viewport stage");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess stage");

    auto&       graph        = *inputs.graph;
    auto&       stageCtx     = *inputs.stageCtx;
    const auto  frameBinding      = inputs.frameBinding;
    const auto& viewportResources = *inputs.viewportResources;
    const auto& viewportRTSpec    = *inputs.viewportRTSpec;

    ShadowGraphOutputs shadowOutputs;
    if (deps.shadowStage && inputs.bEnableShadow) {
        shadowOutputs = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }

    const auto importedResources = importForwardViewportResources(graph, viewportResources, viewportRTSpec, shadowOutputs);
    auto       viewportPasses    = buildForwardViewportPassBundle(inputs, importedResources);

    appendForwardViewportPasses(graph,
                                *deps.viewportStage,
                                stageCtx,
                                frameBinding,
                                inputs.viewportPassContext,
                                importedResources,
                                importedResources.shadowDepth,
                                std::move(viewportPasses));
    appendForwardPostprocessPasses(
        graph,
        *deps.postProcessStage,
        importedResources,
        inputs.postContext,
        inputs.bPostprocessOutputIsSRGB);
}

} // namespace ya
