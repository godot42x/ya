#include "Render3D/Forward/ForwardFrameGraphPasses.h"

#include "RHI/Core/RenderTargetCreateInfo.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/ViewTargetStore.h"

#include <string>
#include <string_view>
#include <utility>

namespace ya::forward_frame_graph
{

namespace
{

constexpr std::string_view kTopologyPassOpaque      = "Forward Opaque";
constexpr std::string_view kTopologyPassSkybox      = "Forward Skybox";
constexpr std::string_view kTopologyPassTransparent = "Forward Transparent";
struct OpaquePassParams
{
    RGTextureHandle                    viewColor{};
    RGTextureHandle                    viewDepth{};
    Rect2D                             renderArea{};
    uint32_t                           layerCount = 1;
    EImageLayout::T                    finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::vector<ForwardDirectionGizmoInput> directionGizmos{};
};

struct SkyboxPassParams
{
    RGTextureHandle viewColor{};
    RGTextureHandle viewDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct EntityIdPassParams
{
    RGTextureHandle viewColor{};
    RGTextureHandle viewDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct TransparentPassParams
{
    RGTextureHandle viewColor{};
    RGTextureHandle viewDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct ViewPassParams
{
    OpaquePassParams      opaque{};
    SkyboxPassParams      skybox{};
    TransparentPassParams transparent{};
    EntityIdPassParams    entityId{};
};

AttachmentDescription makeEntityIdAttachmentDesc()
{
    return AttachmentDescription{
        .format   = EFormat::R32_UINT,
        .samples  = ESampleCount::Sample_1,
        .loadOp   = EAttachmentLoadOp::Clear,
        .storeOp  = EAttachmentStoreOp::Store,
        .finalLayout = EImageLayout::ColorAttachmentOptimal,
        .usage    = EImageUsage::ColorAttachment | EImageUsage::TransferSrc,
    };
}

ViewPassParams buildViewPassParams(const BuildInputs& inputs,
                                           const ViewGraphResources& resources)
{
    return ViewPassParams{
        .opaque = {
            .viewColor   = resources.color,
            .viewDepth   = resources.depth,
            .renderArea      = resources.renderArea,
            .layerCount      = 1,
            .finalLayout     = EImageLayout::ColorAttachmentOptimal,
            .directionGizmos = inputs.directionGizmos,
        },
        .skybox = {
            .viewColor = resources.color,
            .viewDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .transparent = {
            .viewColor = resources.color,
            .viewDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
        .entityId = {
            .viewColor = resources.entityId,
            .viewDepth = resources.depth,
            .renderArea    = resources.renderArea,
            .layerCount    = 1,
            .finalLayout   = EImageLayout::ColorAttachmentOptimal,
        },
    };
}

void appendOpaquePass(RenderGraph& graph,
                      const Dependencies& deps,
                      const BuildInputs& inputs,
                      const ViewGraphResources& resources,
                      OpaquePassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName(kTopologyPassOpaque, inputs.viewId),
        [&params, &resources, predecessor = inputs.familyPredecessor](RGPassBuilder& passBuilder) {
            if (predecessor.has_value()) {
                passBuilder.dependsOn(*predecessor);
            }
            if (resources.shadowDepth.has_value()) {
                passBuilder.read(*resources.shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .clearValue  = ClearValue::black(),
                    .loadOp      = resources.colorAttachment.loadOp,
                    .storeOp     = resources.colorAttachment.storeOp,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = resources.depthAttachment.loadOp,
                    .storeOp     = resources.depthAttachment.storeOp,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stage = deps.viewStage,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding,
         directionGizmos = std::move(params.directionGizmos),
         passContext = inputs.viewPassContext](RGRenderContext& rgCtx) mutable {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewExtent = viewExtent;
            stage->executePBR(*stageCtx, frameBinding, passContext);
            stage->executePhong(*stageCtx, frameBinding, passContext);
            stage->executeUnlit(*stageCtx, frameBinding, passContext);
            stage->executeSimple(*stageCtx, passContext);
            stage->executeDirection(*stageCtx, std::move(directionGizmos), passContext);
            stage->executeDebug(*stageCtx, passContext);
            rgCtx.endRendering();
        });
}

void appendSkyboxPass(RenderGraph& graph,
                      const Dependencies& deps,
                      const BuildInputs& inputs,
                      const ViewGraphResources& resources,
                      SkyboxPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName(kTopologyPassSkybox, inputs.viewId),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stage = deps.viewStage,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding,
         passContext = inputs.viewPassContext](RGRenderContext& rgCtx) {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewExtent = viewExtent;
            stage->executeSkybox(*stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });
}

void appendTransparentPass(RenderGraph& graph,
                           const Dependencies& deps,
                           const BuildInputs& inputs,
                           const ViewGraphResources& resources,
                           TransparentPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName(kTopologyPassTransparent, inputs.viewId),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stageCtx = inputs.stageCtx](RGRenderContext& rgCtx) {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewExtent = viewExtent;
            rgCtx.endRendering();
        });
}

void appendEntityIdPass(RenderGraph& graph,
                        const Dependencies& deps,
                        const BuildInputs& inputs,
                        const ViewGraphResources& resources,
                        EntityIdPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName("Forward EntityId", inputs.viewId),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = resources.entityIdAttachment.loadOp,
                    .storeOp     = resources.entityIdAttachment.storeOp,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [entityIdPass = deps.entityIdPass,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding,
         entityIdBindings = inputs.viewResources ? inputs.viewResources->entityId : EntityIdPassBindings{}](RGRenderContext& rgCtx) {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewExtent = viewExtent;
            if (stageCtx->frameData) {
                entityIdPass->execute(&rgCtx.getCommandBuffer(),
                                      viewExtent.width,
                                      viewExtent.height,
                                      stageCtx->frameData->projection * stageCtx->frameData->view,
                                      stageCtx->frameData->view,
                                      *stageCtx->frameData,
                                      frameBinding.skinningDescriptorSet,
                                      entityIdBindings);
            }
            rgCtx.endRendering();
        });
}


} // namespace

ViewGraphResources createViewResources(RenderGraph&                   graph,
                                               const RenderTargetCreateInfo& viewRTSpec,
                                               std::optional<RGTextureHandle> shadowDepth,
                                               uint64_t                       viewId,
                                               const ViewTargetLease&         targets)
{
    const auto colorAttachment = viewRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *viewRTSpec.attachments.depthAttach;
    const auto importTarget = [&](EViewAttachment role, std::string_view label, EImageLayout::T finalLayout, EImageUsage::T usage) {
        auto target = targets.find(role);
        YA_CORE_ASSERT(target != nullptr, "Forward View target is missing role {}", static_cast<uint32_t>(role));
        return graph.importTexture(makeImportedTextureDesc(*target, label, finalLayout, usage));
    };
    const auto color = importTarget(EViewAttachment::SceneColor, "ForwardView.Color", EImageLayout::ShaderReadOnlyOptimal, colorAttachment.usage);
    const RGTextureHandle resolve{};
    const auto depth = importTarget(EViewAttachment::SceneDepth, "ForwardView.Depth", EImageLayout::DepthStencilAttachmentOptimal, depthAttachment.usage);
    const auto entityIdAttachment = makeEntityIdAttachmentDesc();
    const auto entityId = importTarget(EViewAttachment::EntityId, "ForwardView.EntityId", EImageLayout::ColorAttachmentOptimal, entityIdAttachment.usage);

    return ViewGraphResources{
        .color            = color,
        .resolve          = resolve,
        .depth            = depth,
        .entityId         = entityId,
        .display          = importTarget(EViewAttachment::DisplayColor, "ForwardView.Display", EImageLayout::ShaderReadOnlyOptimal, EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc),
        .bloomExtract     = targets.find(EViewAttachment::BloomExtract) ? importTarget(EViewAttachment::BloomExtract, "ForwardView.BloomExtract", EImageLayout::ShaderReadOnlyOptimal, EImageUsage::ColorAttachment | EImageUsage::Sampled) : RGTextureHandle{},
        .bloomBlur        = targets.find(EViewAttachment::BloomBlur) ? importTarget(EViewAttachment::BloomBlur, "ForwardView.BloomBlur", EImageLayout::ShaderReadOnlyOptimal, EImageUsage::ColorAttachment | EImageUsage::Sampled) : RGTextureHandle{},
        .bloomComposite   = targets.find(EViewAttachment::BloomComposite) ? importTarget(EViewAttachment::BloomComposite, "ForwardView.BloomComposite", EImageLayout::ShaderReadOnlyOptimal, EImageUsage::ColorAttachment | EImageUsage::Sampled) : RGTextureHandle{},
        .shadowDepth      = shadowDepth,
        .viewExtent   = viewRTSpec.extent,
        .colorAttachment  = colorAttachment,
        .depthAttachment  = depthAttachment,
        .entityIdAttachment = entityIdAttachment,
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = viewRTSpec.extent.toVec2()},
    };
}

void appendViewPasses(RenderGraph&                     graph,
                          const Dependencies&              deps,
                          const BuildInputs&               inputs,
                          const ViewGraphResources&    resources)
{
    const auto params = buildViewPassParams(inputs, resources);
    appendOpaquePass(graph, deps, inputs, resources, params.opaque);
    appendSkyboxPass(graph, deps, inputs, resources, params.skybox);
    appendTransparentPass(graph, deps, inputs, resources, params.transparent);
    appendEntityIdPass(graph, deps, inputs, resources, params.entityId);
}

void appendPostprocessPasses(RenderGraph&                  graph,
                             const Dependencies&           deps,
                             const BuildInputs&            inputs,
                             const ViewGraphResources& resources)
{
    const auto postprocessInput = resources.resolve.isValid() ? resources.resolve : resources.color;
    const auto bloomComposite   = deps.postProcessStage->appendBloomGraphPasses(
        graph,
        postprocessInput,
        resources.viewExtent,
        inputs.postContext,
        inputs.viewId,
        inputs.viewResources ? inputs.viewResources->post.bloom : BloomPassBindings{},
        resources.bloomExtract,
        resources.bloomBlur,
        resources.bloomComposite);
    const auto finalizeInput = bloomComposite.isValid() ? bloomComposite : postprocessInput;
    [[maybe_unused]] const auto postprocessOutput = deps.postProcessStage->appendFinalizeGraphPasses(
        graph,
        PostProcessingStage::FinalizePassParams{
            .input         = finalizeInput,
            .output        = resources.display,
            .inputExtent   = resources.viewExtent,
            .bOutputIsSRGB = inputs.bPostprocessOutputIsSRGB,
            .postContext   = inputs.postContext,
            .viewId        = inputs.viewId,
            .toneMap       = inputs.viewResources ? inputs.viewResources->post.toneMap : ToneMapPassBindings{},
        });
}

void exportGraphOutputs(RenderGraph& graph, const ViewGraphResources& resources, uint64_t viewId)
{
    graph.exportTexture(resources.color, makeViewGraphName(forward_graph_exports::viewColor, viewId));
    graph.exportTexture(resources.depth, makeViewGraphName(forward_graph_exports::viewDepth, viewId));
    if (resources.resolve.isValid()) {
        graph.exportTexture(resources.resolve, makeViewGraphName(forward_graph_exports::viewResolve, viewId));
    }
    graph.exportTexture(resources.entityId, makeViewGraphName(forward_graph_exports::entityId, viewId));
}

} // namespace ya::forward_frame_graph
