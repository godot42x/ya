#include "ForwardFrameGraphPasses.h"

#include "Render/Core/RenderTargetCreateInfo.h"
#include "Runtime/Rendering/Common/EntityIdViewportPass.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"

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
constexpr std::string_view kTopologyPassOverlay    = "Forward Overlay";

struct OpaquePassParams
{
    RGTextureHandle                    viewportColor{};
    RGTextureHandle                    viewportDepth{};
    Rect2D                             renderArea{};
    uint32_t                           layerCount = 1;
    EImageLayout::T                    finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::vector<ForwardDirectionGizmoInput> directionGizmos{};
};

struct SkyboxPassParams
{
    RGTextureHandle viewportColor{};
    RGTextureHandle viewportDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct EntityIdPassParams
{
    RGTextureHandle viewportColor{};
    RGTextureHandle viewportDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct TransparentPassParams
{
    RGTextureHandle viewportColor{};
    RGTextureHandle viewportDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct OverlayPassParams
{
    RGTextureHandle color{};
    RGTextureHandle depth{};
    RGTextureHandle resolve{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::shared_ptr<const RenderViewportOverlaySnapshot> snapshot = nullptr;
    FrameContext frameCtx{};
};

struct ViewportPassParams
{
    OpaquePassParams      opaque{};
    SkyboxPassParams      skybox{};
    TransparentPassParams transparent{};
    EntityIdPassParams    entityId{};
    OverlayPassParams     overlay{};
};

RGTextureDesc makeViewportTextureDesc(const AttachmentDescription& attachment,
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

ViewportPassParams buildViewportPassParams(const BuildInputs& inputs,
                                           const ViewportGraphResources& resources)
{
    return ViewportPassParams{
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
            .color        = resources.color,
            .depth        = resources.depth,
            .resolve      = resources.resolve,
            .renderArea   = resources.renderArea,
            .layerCount   = 1,
            .finalLayout  = resources.colorAttachment.finalLayout,
            .snapshot     = inputs.viewportOverlaySnapshot,
            .frameCtx     = inputs.postContext ? *inputs.postContext : FrameContext{},
        },
    };
}

void appendOpaquePass(RenderGraph& graph,
                      const Dependencies& deps,
                      const BuildInputs& inputs,
                      const ViewportGraphResources& resources,
                      OpaquePassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        std::string(kTopologyPassOpaque),
        [&params, &resources](RGPassBuilder& passBuilder) {
            if (resources.shadowDepth.has_value()) {
                passBuilder.read(*resources.shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewportColor,
                    .clearValue  = ClearValue::Black(),
                    .loadOp      = resources.colorAttachment.loadOp,
                    .storeOp     = resources.colorAttachment.storeOp,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewportDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = resources.depthAttachment.loadOp,
                    .storeOp     = resources.depthAttachment.storeOp,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stage = deps.viewportStage,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding,
         directionGizmos = std::move(params.directionGizmos),
         passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewportExtent = viewportExtent;
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
                      const ViewportGraphResources& resources,
                      SkyboxPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        std::string(kTopologyPassSkybox),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stage = deps.viewportStage,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding,
         passContext = inputs.viewportPassContext](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewportExtent = viewportExtent;
            stage->executeSkybox(*stageCtx, frameBinding, passContext);
            rgCtx.endRendering();
        });
}

void appendTransparentPass(RenderGraph& graph,
                           const Dependencies& deps,
                           const BuildInputs& inputs,
                           const ViewportGraphResources& resources,
                           TransparentPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        std::string(kTopologyPassTransparent),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stageCtx = inputs.stageCtx](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewportExtent = viewportExtent;
            rgCtx.endRendering();
        });
}

void appendEntityIdPass(RenderGraph& graph,
                        const Dependencies& deps,
                        const BuildInputs& inputs,
                        const ViewportGraphResources& resources,
                        EntityIdPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        std::string("Forward EntityId"),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewportColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = resources.entityIdAttachment.loadOp,
                    .storeOp     = resources.entityIdAttachment.storeOp,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [entityIdPass = deps.entityIdPass,
         stageCtx = inputs.stageCtx,
         frameBinding = inputs.frameBinding](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewportExtent = viewportExtent;
            if (stageCtx->frameData) {
                entityIdPass->execute(&rgCtx.getCommandBuffer(),
                                      viewportExtent.width,
                                      viewportExtent.height,
                                      stageCtx->frameData->projection * stageCtx->frameData->view,
                                      stageCtx->frameData->view,
                                      *stageCtx->frameData,
                                      frameBinding.skinningDescriptorSet);
            }
            rgCtx.endRendering();
        });
}

void appendOverlayPass(RenderGraph& graph,
                       const BuildInputs& inputs,
                       const ViewportGraphResources& resources,
                       OverlayPassParams params)
{
    [[maybe_unused]] const auto pass = graph.addPass(
        std::string(kTopologyPassOverlay),
        [&params, &resources](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.color,
                    .resolve     = params.resolve,
                    .resolveMode = params.resolve.isValid() ? EResolveMode::Average : EResolveMode::None,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = params.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = resources.depthAttachment.finalLayout,
                },
            });
        },
        [stageCtx = inputs.stageCtx, params = std::move(params)](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            stageCtx->viewportExtent = viewportExtent;
            params.frameCtx.extent = viewportExtent;
            recordRenderViewportOverlayPass(
                params.frameCtx,
                params.snapshot,
                &rgCtx.getCommandBuffer());
            rgCtx.endRendering();
        });
}

} // namespace

ViewportGraphResources createViewportResources(RenderGraph&                   graph,
                                               const RenderTargetCreateInfo& viewportRTSpec,
                                               std::optional<RGTextureHandle> shadowDepth)
{
    const auto colorAttachment = viewportRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *viewportRTSpec.attachments.depthAttach;
    const uint32_t layerCount = viewportRTSpec.layerCount;
    const auto color = graph.createPersistentTexture(
        makeViewportTextureDesc(colorAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.Color"),
        RGPersistentTextureKey{.value = "ForwardViewport.Color"});
    const RGTextureHandle resolve = viewportRTSpec.attachments.resolveAttach.has_value()
        ? graph.createPersistentTexture(
              makeViewportTextureDesc(
                  *viewportRTSpec.attachments.resolveAttach,
                  viewportRTSpec.extent,
                  layerCount,
                  "ForwardViewport.Resolve"),
              RGPersistentTextureKey{.value = "ForwardViewport.Resolve"})
        : RGTextureHandle{};
    const auto depth = graph.createPersistentTexture(
        makeViewportTextureDesc(depthAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.Depth"),
        RGPersistentTextureKey{.value = "ForwardViewport.Depth"});
    const auto entityIdAttachment = makeEntityIdAttachmentDesc();
    const auto entityId = graph.createPersistentTexture(
        makeViewportTextureDesc(entityIdAttachment, viewportRTSpec.extent, layerCount, "ForwardViewport.EntityId"),
        RGPersistentTextureKey{.value = "ForwardViewport.EntityId"});

    return ViewportGraphResources{
        .color            = color,
        .resolve          = resolve,
        .depth            = depth,
        .entityId         = entityId,
        .shadowDepth      = shadowDepth,
        .viewportExtent   = viewportRTSpec.extent,
        .colorAttachment  = colorAttachment,
        .depthAttachment  = depthAttachment,
        .entityIdAttachment = entityIdAttachment,
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = viewportRTSpec.extent.toVec2()},
    };
}

void appendViewportPasses(RenderGraph&                     graph,
                          const Dependencies&              deps,
                          const BuildInputs&               inputs,
                          const ViewportGraphResources&    resources)
{
    const auto params = buildViewportPassParams(inputs, resources);
    appendOpaquePass(graph, deps, inputs, resources, params.opaque);
    appendSkyboxPass(graph, deps, inputs, resources, params.skybox);
    appendTransparentPass(graph, deps, inputs, resources, params.transparent);
    appendEntityIdPass(graph, deps, inputs, resources, params.entityId);
    appendOverlayPass(graph, inputs, resources, params.overlay);
}

void appendPostprocessPasses(RenderGraph&                  graph,
                             const Dependencies&           deps,
                             const BuildInputs&            inputs,
                             const ViewportGraphResources& resources)
{
    const auto postprocessInput = resources.resolve.isValid() ? resources.resolve : resources.color;
    const auto bloomComposite   = deps.postProcessStage->appendBloomGraphPasses(
        graph,
        postprocessInput,
        resources.viewportExtent,
        inputs.postContext);
    const auto finalizeInput = bloomComposite.isValid() ? bloomComposite : postprocessInput;
    [[maybe_unused]] const auto postprocessOutput = deps.postProcessStage->appendFinalizeGraphPasses(
        graph,
        PostProcessingStage::FinalizePassParams{
            .input         = finalizeInput,
            .inputExtent   = resources.viewportExtent,
            .bOutputIsSRGB = inputs.bPostprocessOutputIsSRGB,
            .postContext   = inputs.postContext,
        });
}

void exportGraphOutputs(RenderGraph& graph, const ViewportGraphResources& resources)
{
    graph.exportTexture(resources.color, std::string(forward_graph_exports::viewportColor));
    graph.exportTexture(resources.depth, std::string(forward_graph_exports::viewportDepth));
    if (resources.resolve.isValid()) {
        graph.exportTexture(resources.resolve, std::string(forward_graph_exports::viewportResolve));
    }
    graph.exportTexture(resources.entityId, std::string(forward_graph_exports::entityId));
}

} // namespace ya::forward_frame_graph
