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

enum class EForwardTopologyPass : uint8_t
{
    Shadow,
    Skybox,
    PBR,
    Phong,
    Unlit,
    Simple,
    Direction,
    Debug,
    ViewportOverlay,
    Bloom,
    Postprocessing,
};

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

struct ForwardTopologyEdge
{
    EForwardTopologyPass from;
    EForwardTopologyPass to;
};

struct ForwardTopologyPlan
{
    std::vector<EForwardTopologyPass> order{};
    std::vector<ForwardTopologyEdge>  dependencies{};
};

std::string_view getForwardTopologyPassName(EForwardTopologyPass pass)
{
    switch (pass) {
        case EForwardTopologyPass::Shadow: return kForwardTopologyPassShadow;
        case EForwardTopologyPass::Skybox: return kForwardTopologyPassSkybox;
        case EForwardTopologyPass::PBR: return kForwardTopologyPassPBR;
        case EForwardTopologyPass::Phong: return kForwardTopologyPassPhong;
        case EForwardTopologyPass::Unlit: return kForwardTopologyPassUnlit;
        case EForwardTopologyPass::Simple: return kForwardTopologyPassSimple;
        case EForwardTopologyPass::Direction: return kForwardTopologyPassDirection;
        case EForwardTopologyPass::Debug: return kForwardTopologyPassDebug;
        case EForwardTopologyPass::ViewportOverlay: return kForwardTopologyPassViewportOverlay;
        case EForwardTopologyPass::Bloom: return kForwardTopologyPassBloom;
        case EForwardTopologyPass::Postprocessing: return kForwardTopologyPassPostprocessing;
    }

    YA_CORE_ASSERT(false, "Unhandled forward topology pass");
    return {};
}

ForwardTopologyPlan buildForwardTopologyPlan(const ForwardFrameGraphOrchestrator::TopologyInputs& inputs)
{
    ForwardTopologyPlan plan{};
    if (inputs.bHasShadowSubgraph) {
        plan.order.push_back(EForwardTopologyPass::Shadow);
    }
    plan.order.insert(plan.order.end(),
                      {
                          EForwardTopologyPass::Skybox,
                          EForwardTopologyPass::PBR,
                          EForwardTopologyPass::Phong,
                          EForwardTopologyPass::Unlit,
                          EForwardTopologyPass::Simple,
                          EForwardTopologyPass::Direction,
                          EForwardTopologyPass::Debug,
                          EForwardTopologyPass::ViewportOverlay,
                      });
    if (inputs.bHasBloomSubgraph) {
        plan.order.push_back(EForwardTopologyPass::Bloom);
    }
    if (inputs.bHasPostprocessPass) {
        plan.order.push_back(EForwardTopologyPass::Postprocessing);
    }

    plan.dependencies.insert(plan.dependencies.end(),
                             {
                                 {EForwardTopologyPass::Skybox, EForwardTopologyPass::PBR},
                                 {EForwardTopologyPass::PBR, EForwardTopologyPass::Phong},
                                 {EForwardTopologyPass::Phong, EForwardTopologyPass::Unlit},
                                 {EForwardTopologyPass::Unlit, EForwardTopologyPass::Simple},
                                 {EForwardTopologyPass::Simple, EForwardTopologyPass::Direction},
                                 {EForwardTopologyPass::Direction, EForwardTopologyPass::Debug},
                                 {EForwardTopologyPass::Debug, EForwardTopologyPass::ViewportOverlay},
                             });
    if (inputs.bHasShadowSubgraph) {
        plan.dependencies.push_back({EForwardTopologyPass::Shadow, EForwardTopologyPass::PBR});
        plan.dependencies.push_back({EForwardTopologyPass::Shadow, EForwardTopologyPass::Phong});
    }
    if (inputs.bHasBloomSubgraph) {
        plan.dependencies.push_back({EForwardTopologyPass::ViewportOverlay, EForwardTopologyPass::Bloom});
        if (inputs.bHasPostprocessPass) {
            plan.dependencies.push_back({EForwardTopologyPass::Bloom, EForwardTopologyPass::Postprocessing});
        }
    }
    else if (inputs.bHasPostprocessPass) {
        plan.dependencies.push_back({EForwardTopologyPass::ViewportOverlay, EForwardTopologyPass::Postprocessing});
    }

    return plan;
}

RGImportedTextureDesc makeForwardViewportImportedDesc(const RenderImage& image,
                                                      std::string_view   label,
                                                      EImageLayout::T    finalLayout)
{
    return makeImportedTextureDesc(image, label, finalLayout);
}

} // namespace

ForwardFrameGraphOrchestrator::TopologyDescription ForwardFrameGraphOrchestrator::describeTopology(
    const TopologyInputs& inputs)
{
    const auto plan = buildForwardTopologyPlan(inputs);
    TopologyDescription topology{};
    topology.passOrder.reserve(plan.order.size());
    for (const auto pass : plan.order) {
        topology.passOrder.push_back(getForwardTopologyPassName(pass));
    }
    topology.dependencies.reserve(plan.dependencies.size());
    for (const auto& edge : plan.dependencies) {
        topology.dependencies.emplace_back(getForwardTopologyPassName(edge.from), getForwardTopologyPassName(edge.to));
    }

    return topology;
}

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
    const auto topologyPlan = buildForwardTopologyPlan({
        .bHasShadowSubgraph  = deps.shadowStage && inputs.bEnableShadow,
        .bHasBloomSubgraph   = true,
        .bHasPostprocessPass = true,
    });
    std::optional<RGPassHandle> forwardSkyboxPassHandle;
    std::optional<RGPassHandle> forwardPbrPassHandle;
    std::optional<RGPassHandle> forwardPhongPassHandle;
    std::optional<RGPassHandle> forwardUnlitPassHandle;
    std::optional<RGPassHandle> forwardSimplePassHandle;
    std::optional<RGPassHandle> forwardDirectionPassHandle;
    std::optional<RGPassHandle> forwardDebugPassHandle;
    auto applyTopologyDependencies = [&](RGPassBuilder& passBuilder, EForwardTopologyPass currentPass)
    {
        for (const auto& edge : topologyPlan.dependencies) {
            if (edge.to != currentPass) {
                continue;
            }
            switch (edge.from) {
                case EForwardTopologyPass::Shadow:
                    if (shadowOutputs.lastPass.has_value()) {
                        passBuilder.dependsOn(*shadowOutputs.lastPass);
                    }
                    break;
                case EForwardTopologyPass::Skybox:
                    if (forwardSkyboxPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardSkyboxPassHandle);
                    }
                    break;
                case EForwardTopologyPass::PBR:
                    if (forwardPbrPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardPbrPassHandle);
                    }
                    break;
                case EForwardTopologyPass::Phong:
                    if (forwardPhongPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardPhongPassHandle);
                    }
                    break;
                case EForwardTopologyPass::Unlit:
                    if (forwardUnlitPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardUnlitPassHandle);
                    }
                    break;
                case EForwardTopologyPass::Simple:
                    if (forwardSimplePassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardSimplePassHandle);
                    }
                    break;
                case EForwardTopologyPass::Direction:
                    if (forwardDirectionPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardDirectionPassHandle);
                    }
                    break;
                case EForwardTopologyPass::Debug:
                    if (forwardDebugPassHandle.has_value()) {
                        passBuilder.dependsOn(*forwardDebugPassHandle);
                    }
                    break;
                default:
                    break;
            }
        }
    };

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
        [&skyboxParams, colorAttachment, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Skybox);
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
        [&pbrParams, shadowDepth, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::PBR);
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
        [&phongParams, shadowDepth, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Phong);
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
        [&unlitParams, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Unlit);
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
        [&simpleParams, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Simple);
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
        [&directionParams, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Direction);
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
        [&debugParams, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::Debug);
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
        [&viewportOverlayParams, resolve, depthAttachment, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EForwardTopologyPass::ViewportOverlay);
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
    forwardSkyboxPassHandle = skyboxPass;
    forwardPbrPassHandle = pbrPass;
    forwardPhongPassHandle = phongPass;
    forwardUnlitPassHandle = unlitPass;
    forwardSimplePassHandle = simplePass;
    forwardDirectionPassHandle = directionPass;
    forwardDebugPassHandle = debugPass;

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
