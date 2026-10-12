#include "Render3D/Deferred/DeferredFrameGraphPasses.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Deferred/DeferredPassNames.h"

#include <string>

namespace ya
{

namespace deferred_frame_graph_passes
{

void importFrameBuffers(DeferredFrameGraphPassContext& context)
{
    auto&       graph          = context.graph;
    auto&       graphResources = context.graphResources;
    const auto& frameBinding   = context.frameBinding;

    graphResources.buffers.frame = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        frameBinding.frame.buffer,
        "Deferred.FrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.frame.offset,
        frameBinding.frame.size));
    graphResources.buffers.light = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        frameBinding.light.buffer,
        "Deferred.LightUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.light.offset,
        frameBinding.light.size));
    graphResources.buffers.skinning = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        frameBinding.skinningBuffer,
        "Deferred.SkinningSSBO",
        EBufferUsage::StorageBuffer));
    if (frameBinding.ssaoFrame.valid()) {
        graphResources.buffers.ssaoFrame = graph.importBuffer(makeHostWrittenImportedBufferDesc(
            frameBinding.ssaoFrame.buffer,
            "Deferred.SSAOFrameUBO",
            EBufferUsage::UniformBuffer,
            frameBinding.ssaoFrame.offset,
            frameBinding.ssaoFrame.size));
    }
    graphResources.buffers.skyboxFrame = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        frameBinding.skyboxFrame.buffer,
        "Deferred.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.skyboxFrame.offset,
        frameBinding.skyboxFrame.size));
}

void createTailAttachmentTextures(DeferredFrameGraphPassContext& context)
{
    auto&       graphResources = context.graphResources;
    YA_CORE_ASSERT(context.targets != nullptr, "Deferred graph requires prepared View targets");
    const auto importTarget = [&](EViewAttachment role, std::string_view label, EImageLayout::T finalLayout, EImageUsage::T usage) {
        auto target = context.targets->find(role);
        YA_CORE_ASSERT(target != nullptr, "Deferred View target is missing role {}", static_cast<uint32_t>(role));
        return context.graph.importTexture(makeImportedTextureDesc(*target, label, finalLayout, usage));
    };

    graphResources.textures.entityId = importTarget(
        EViewAttachment::EntityId, "DeferredView.EntityId", EImageLayout::ColorAttachmentOptimal,
        EImageUsage::ColorAttachment | EImageUsage::TransferSrc);
    graphResources.textures.postprocessOutput = importTarget(
        EViewAttachment::DisplayColor, "DeferredView.Display", EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc);
}

void appendForwardOpaque(DeferredFrameGraphPassContext& context)
{
    DeferredForwardOpaquePassParams params{
        .color      = context.graphResources.textures.viewColor,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount = 1,
    };

    (void)context.graph.addPass(
        makeViewGraphName(deferred_pass_names::kForwardOpaque, context.viewId),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [](RGRenderContext& rgCtx) {
            rgCtx.beginDeclaredRasterRendering();
            rgCtx.endRendering();
        });
}

void appendSkybox(DeferredFrameGraphPassContext& context)
{
    const auto& frameBinding = context.frameBinding;
    DeferredSkyboxPassParams params{
        .frame = {
            .handle = context.graphResources.buffers.skyboxFrame,
            .range  = RGBufferRange{
                .offset = frameBinding.skyboxFrame.offset,
                .size   = frameBinding.skyboxFrame.size,
            },
        },
        .viewColor = context.graphResources.textures.viewColor,
        .depth         = context.graphResources.textures.gBufferDepth,
        .renderArea    = {.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount    = 1,
        .skybox        = context.overlayInputs
            ? context.overlayInputs->skybox
            : ViewOverlayStage::FrameInputs::SkyboxInput{},
    };

    context.graphResources.passes.skybox = context.graph.addPass(
        makeViewGraphName(deferred_pass_names::kSkybox, context.viewId),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(params.frame.handle, params.frame.range);
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx = context.stageCtx, params, overlayStage = &context.overlayStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            overlayStage->executeSkybox(stageCtx, params.skybox);
            rgCtx.endRendering();
        });
}

void appendSprite2D(DeferredFrameGraphPassContext& context)
{
    if (!context.spriteStage) {
        return;
    }

    // Content gate: a View without sprites has no sprite workload at all, so the
    // pass is not even added. Recording reads the View's own candidates, so the
    // gate is a bucket lookup, not an ECS walk.
    const RenderFrameData* frameData = context.stageCtx.frameData;
    if (!frameData || frameData->worldSprites.empty()) {
        return;
    }

    DeferredSprite2DPassParams params{
        .color      = context.graphResources.textures.viewColor,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount = 1,
        .bindings   = context.viewResources ? context.viewResources->sprite : Sprite2DPassBindings{},
    };

    (void)context.graph.addPass(
        makeViewGraphName(deferred_pass_names::kSprites, context.viewId),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    .bReadOnly   = true,
                },
            });
        },
        [stageCtx = context.stageCtx, params, spriteStage = context.spriteStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            spriteStage->executeSprites(stageCtx, params.bindings);
            rgCtx.endRendering();
        });
}

void appendBloom(DeferredFrameGraphPassContext& context)
{
    const auto importBloom = [&](EViewAttachment role, std::string_view label) {
        auto target = context.targets ? context.targets->find(role) : nullptr;
        return target
            ? context.graph.importTexture(makeImportedTextureDesc(
                  *target, label, EImageLayout::ShaderReadOnlyOptimal,
                  EImageUsage::ColorAttachment | EImageUsage::Sampled))
            : RGTextureHandle{};
    };
    const auto bloomComposite = context.postProcessStage.appendBloomGraphPasses(
        context.graph,
        context.graphResources.textures.viewColor,
        context.viewExtent,
        context.postContext,
        context.viewId,
        context.viewResources ? context.viewResources->post.bloom : BloomPassBindings{},
        importBloom(EViewAttachment::BloomExtract, "Deferred.BloomExtract"),
        importBloom(EViewAttachment::BloomBlur, "Deferred.BloomBlur"),
        importBloom(EViewAttachment::BloomComposite, "Deferred.BloomComposite"));
    if (bloomComposite.isValid()) {
        context.graphResources.textures.bloomComposite = bloomComposite;
    }
    context.graphResources.textures.overlayInput = bloomComposite.isValid()
        ? bloomComposite
        : context.graphResources.textures.viewColor;
}

void appendForwardTransparent(DeferredFrameGraphPassContext& context)
{
    DeferredForwardTransparentPassParams params{
        .color      = context.graphResources.textures.overlayInput,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount = 1,
        .overlay    = context.overlayInputs
            ? *context.overlayInputs
            : ViewOverlayStage::FrameInputs{},
        .overlayBindings = context.viewResources ? context.viewResources->overlay : OverlayPassBindings{},
    };

    context.graphResources.passes.sceneOverlay = context.graph.addPass(
        makeViewGraphName(deferred_pass_names::kForwardTransparent, context.viewId),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx = context.stageCtx, params, overlayStage = &context.overlayStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
            overlayStage->executeOverlay(stageCtx, params.overlay, params.overlayBindings);
            rgCtx.endRendering();
        });
}

void appendEntityId(DeferredFrameGraphPassContext& context)
{
    const auto entityId = context.graphResources.textures.entityId;
    const auto depth    = context.graphResources.textures.gBufferDepth;
    const auto extent   = context.viewExtent;
    const auto stageCtx = context.stageCtx;
    const auto frameBinding = context.frameBinding;
    auto* const entityIdPass = context.entityIdPass;

    // Billboards are only rendered by the overlay pass (which runs after this
    // pass), so carry their camera-facing quads into the id pass to keep the
    // id target aligned with what is visible.
    std::vector<EntityIdBillboard> billboards;
    if (context.overlayInputs) {
        billboards.reserve(context.overlayInputs->billboards.size());
        for (const auto& billboard : context.overlayInputs->billboards) {
            billboards.push_back(EntityIdBillboard{
                .worldCenter = billboard.worldCenter,
                .worldSize   = billboard.worldSize,
                .entityId    = billboard.entityId,
            });
        }
    }

    (void)context.graph.addPass(
        makeViewGraphName(deferred_pass_names::kEntityId, context.viewId),
        [entityId, depth, extent](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = extent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = entityId,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ColorAttachmentOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, entityIdPass, frameBinding, entityIdBindings = context.viewResources ? context.viewResources->entityId : EntityIdPassBindings{}, billboards = std::move(billboards)](RGRenderContext& rgCtx) {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            if (entityIdPass && stageCtx.frameData) {
                entityIdPass->execute(&rgCtx.getCommandBuffer(),
                                      viewExtent.width,
                                      viewExtent.height,
                                      stageCtx.frameData->projection * stageCtx.frameData->view,
                                      stageCtx.frameData->view,
                                      *stageCtx.frameData,
                                      frameBinding.skinningDescriptorSet,
                                      entityIdBindings,
                                      billboards);
            }
            rgCtx.endRendering();
        });
}


void appendPostprocess(DeferredFrameGraphPassContext& context)
{
    const auto postprocessOutput = context.postProcessStage.appendFinalizeGraphPasses(
        context.graph,
        PostProcessingStage::FinalizePassParams{
            .input         = context.graphResources.textures.overlayInput,
            .output        = context.graphResources.textures.postprocessOutput.value_or(RGTextureHandle{}),
            .inputExtent   = context.viewExtent,
            .bOutputIsSRGB = context.bPostprocessOutputIsSRGB,
            .postContext   = context.postContext,
            .viewId        = context.viewId,
            .toneMap       = context.viewResources ? context.viewResources->post.toneMap : ToneMapPassBindings{},
        });
    if (postprocessOutput.isValid()) {
        context.graphResources.textures.postprocessOutput = postprocessOutput;
    }
}

} // namespace deferred_frame_graph_passes

} // namespace ya
