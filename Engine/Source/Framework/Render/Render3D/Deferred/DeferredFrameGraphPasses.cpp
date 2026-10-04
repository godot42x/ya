#include "Render3D/Deferred/DeferredFrameGraphPasses.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/Shadow/ShadowStage.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewTargetStore.h"

#include <string>

namespace ya
{

namespace
{

constexpr std::string_view kTopologyPassGBuffer            = "Deferred GBuffer";
constexpr std::string_view kTopologyPassLight             = "Deferred Light";
constexpr std::string_view kTopologyPassForwardOpaque     = "Deferred Forward Opaque";
constexpr std::string_view kTopologyPassSkybox            = "Deferred Skybox";
constexpr std::string_view kTopologyPassSprites           = "Deferred Sprites";
constexpr std::string_view kTopologyPassForwardTransparent = "Deferred Forward Transparent";

} // namespace

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
    if (context.bUseSSAO) {
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

void createAttachmentTextures(DeferredFrameGraphPassContext& context)
{
    auto&       graphResources = context.graphResources;
    const auto& gBufferSpec    = context.gBufferRTSpec;
    const auto& viewportSpec   = context.viewRTSpec;
    YA_CORE_ASSERT(context.targets != nullptr, "Deferred graph requires prepared View targets");
    const auto importTarget = [&](EViewAttachment role, std::string_view label, EImageLayout::T finalLayout, EImageUsage::T usage) {
        auto target = context.targets->find(role);
        YA_CORE_ASSERT(target != nullptr, "Deferred View target is missing role {}", static_cast<uint32_t>(role));
        return context.graph.importTexture(makeImportedTextureDesc(*target, label, finalLayout, usage));
    };

    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        const auto label = std::format("DeferredGBuffer.Color{}", attachmentIndex);
        graphResources.textures.gBufferColors[attachmentIndex] = importTarget(
            static_cast<EViewAttachment>(static_cast<uint8_t>(EViewAttachment::GBuffer0) + attachmentIndex),
            label, EImageLayout::ShaderReadOnlyOptimal, gBufferSpec.attachments.colorAttach[attachmentIndex].usage);
    }
    YA_CORE_ASSERT(gBufferSpec.attachments.depthAttach.has_value(),
                   "Deferred GBuffer graph requires a depth attachment spec");
    graphResources.textures.gBufferDepth = importTarget(
        EViewAttachment::SceneDepth, "DeferredGBuffer.Depth", EImageLayout::ShaderReadOnlyOptimal,
        gBufferSpec.attachments.depthAttach->usage);

    YA_CORE_ASSERT(!viewportSpec.attachments.colorAttach.empty(),
                   "Deferred viewport graph requires a color attachment spec");
    graphResources.textures.viewColor = importTarget(
        EViewAttachment::SceneColor, "DeferredView.Color", EImageLayout::ShaderReadOnlyOptimal,
        viewportSpec.attachments.colorAttach.front().usage);

    AttachmentDescription entityIdDesc{};
    entityIdDesc.format      = EFormat::R32_UINT;
    entityIdDesc.samples     = ESampleCount::Sample_1;
    entityIdDesc.loadOp      = EAttachmentLoadOp::Clear;
    entityIdDesc.storeOp     = EAttachmentStoreOp::Store;
    entityIdDesc.usage       = EImageUsage::ColorAttachment | EImageUsage::TransferSrc;
    entityIdDesc.finalLayout = EImageLayout::ColorAttachmentOptimal;
    graphResources.textures.entityId = importTarget(
        EViewAttachment::EntityId, "DeferredView.EntityId", EImageLayout::ColorAttachmentOptimal, entityIdDesc.usage);
    graphResources.textures.postprocessOutput = importTarget(
        EViewAttachment::DisplayColor, "DeferredView.Display", EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc);
}

void appendGBuffer(DeferredFrameGraphPassContext& context)
{
    auto&       graph          = context.graph;
    auto&       graphResources = context.graphResources;
    const auto& frameBinding  = context.frameBinding;
    const auto& gbufferExtent = context.gBufferRTSpec.extent;

    DeferredGBufferPassParams params{
        .frame = {
            .handle = graphResources.buffers.frame,
            .range  = RGBufferRange{.offset = frameBinding.frame.offset, .size = frameBinding.frame.size},
        },
        .light = {
            .handle = graphResources.buffers.light,
            .range  = RGBufferRange{.offset = frameBinding.light.offset, .size = frameBinding.light.size},
        },
        .skinning                    = graphResources.buffers.skinning,
        .gBufferColors               = graphResources.textures.gBufferColors,
        .gBufferDepth                = graphResources.textures.gBufferDepth,
        .renderArea                  = Rect2D{.pos = {0, 0}, .extent = gbufferExtent.toVec2()},
        .layerCount                  = 1,
        .frameAndLightDescriptorSet  = frameBinding.frameAndLightDescriptorSet,
        .skinningDescriptorSet       = frameBinding.skinningDescriptorSet,
    };

    graphResources.passes.gBuffer = graph.addPass(
        makeViewGraphName(kTopologyPassGBuffer, context.viewId),
        [&params, predecessor = context.familyPredecessor](RGPassBuilder& passBuilder) {
            if (predecessor.has_value()) {
                passBuilder.dependsOn(*predecessor);
            }
            passBuilder.uniformRead(params.frame.handle, params.frame.range);
            passBuilder.uniformRead(params.light.handle, params.light.range);
            passBuilder.storageRead(params.skinning);
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {
                    {.color = params.gBufferColors[0], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = params.gBufferColors[1], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = params.gBufferColors[2], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = params.gBufferColors[3], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGDepthAttachmentDesc{
                    .depth       = params.gBufferDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx = context.stageCtx, params, gBufferStage = &context.gBufferStage, bReverseViewportY = context.bReverseViewportY](RGRenderContext& rgCtx) {
            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            [[maybe_unused]] IBuffer* const frameBuffer    = rgCtx.resolveBuffer(params.frame.handle);
            [[maybe_unused]] IBuffer* const lightBuffer    = rgCtx.resolveBuffer(params.light.handle);
            [[maybe_unused]] IBuffer* const skinningBuffer = rgCtx.resolveBuffer(params.skinning);

            rgCtx.beginDeclaredRasterRendering();

            const auto gbufferExtent = rasterParams.getRenderExtent();
            const auto vpW           = gbufferExtent.width;
            const auto vpH           = gbufferExtent.height;
            const float gbVpY         = bReverseViewportY ? static_cast<float>(vpH) : 0.0f;
            const float gbVpH         = bReverseViewportY ? -static_cast<float>(vpH) : static_cast<float>(vpH);
            rgCtx.getCommandBuffer().setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
            rgCtx.getCommandBuffer().setScissor(0, 0, vpW, vpH);

            gBufferStage->execute(stageCtx, GBufferStage::FrameInputs{
                .frameAndLightDescriptorSet = params.frameAndLightDescriptorSet,
                .skinningDescriptorSet      = params.skinningDescriptorSet,
            });
            rgCtx.endRendering();
        });
}

void appendSSAO(DeferredFrameGraphPassContext& context)
{
    if (!context.bUseSSAO) {
        return;
    }

    YA_CORE_ASSERT(context.ssaoStage != nullptr, "Deferred SSAO requires an SSAO stage");
    YA_CORE_ASSERT(context.graphResources.buffers.ssaoFrame.has_value(),
                   "Deferred SSAO requires an imported frame buffer");

    const auto& frameBinding = context.frameBinding;
    auto ssaoTarget = context.targets ? context.targets->find(EViewAttachment::SSAO) : nullptr;
    YA_CORE_ASSERT(ssaoTarget != nullptr, "Deferred SSAO requires a prepared target");
    const auto ssaoOutput = context.graph.importTexture(makeImportedTextureDesc(
        *ssaoTarget, "Deferred.SSAO", EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::ColorAttachment | EImageUsage::Sampled));
    context.graphResources.textures.ssao = context.ssaoStage->appendGraphPass(
        context.graph,
        context.stageCtx,
        DeferredSSAOPassParams{
            .frame              = *context.graphResources.buffers.ssaoFrame,
            .frameRange         = RGBufferRange{.offset = frameBinding.ssaoFrame.offset, .size = frameBinding.ssaoFrame.size},
            .albedo             = context.graphResources.textures.gBufferColors[0],
            .normal             = context.graphResources.textures.gBufferColors[1],
            .depth              = context.graphResources.textures.gBufferDepth,
            .frameDescriptorSet = frameBinding.ssaoFrameDescriptorSet,
            .inputDescriptorSet = context.viewResources ? context.viewResources->ssao.inputs.set : DescriptorSetHandle{},
            .viewId             = context.viewId,
        },
        ssaoOutput);
}

void appendLight(DeferredFrameGraphPassContext& context)
{
    auto&       graph          = context.graph;
    auto&       graphResources = context.graphResources;
    const auto& frameBinding  = context.frameBinding;

    if (context.environmentLighting && context.environmentLighting->isComplete()) {
        graphResources.textures.environmentCubemap = graph.importTexture(
            makeImportedTextureDesc(context.environmentLighting->cubemap,
                                    "DeferredLight.Environment.Cubemap",
                                    EImageLayout::ShaderReadOnlyOptimal));
        graphResources.textures.environmentIrradiance = graph.importTexture(
            makeImportedTextureDesc(context.environmentLighting->irradiance,
                                    "DeferredLight.Environment.Irradiance",
                                    EImageLayout::ShaderReadOnlyOptimal));
        graphResources.textures.environmentPrefilter = graph.importTexture(
            makeImportedTextureDesc(context.environmentLighting->prefilter,
                                    "DeferredLight.Environment.Prefilter",
                                    EImageLayout::ShaderReadOnlyOptimal));
        graphResources.textures.environmentBrdfLut = graph.importTexture(
            makeImportedTextureDesc(*context.environmentLighting->brdfLut,
                                    "DeferredLight.Environment.BrdfLut",
                                    EImageLayout::ShaderReadOnlyOptimal));
    }

    graphResources.textures.shadowDepth = graphResources.passes.shadow.shadowDepth;

    DeferredLightPassParams params{
        .frame = {
            .handle = graphResources.buffers.frame,
            .range  = RGBufferRange{.offset = frameBinding.frame.offset, .size = frameBinding.frame.size},
        },
        .light = {
            .handle = graphResources.buffers.light,
            .range  = RGBufferRange{.offset = frameBinding.light.offset, .size = frameBinding.light.size},
        },
        .gBufferColors                    = graphResources.textures.gBufferColors,
        .gBufferDepth                     = graphResources.textures.gBufferDepth,
        .ssao                             = graphResources.textures.ssao,
        .environmentCubemap               = graphResources.textures.environmentCubemap,
        .environmentIrradiance            = graphResources.textures.environmentIrradiance,
        .environmentPrefilter             = graphResources.textures.environmentPrefilter,
        .environmentBrdfLut               = graphResources.textures.environmentBrdfLut,
        .shadowDepth                      = graphResources.textures.shadowDepth,
        .viewColor                    = graphResources.textures.viewColor,
        .renderArea                      = Rect2D{.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount                      = 1,
        .frameAndLightDescriptorSet      = frameBinding.frameAndLightDescriptorSet,
        .environmentLightingDescriptorSet = context.environmentLightingDS,
        .gBufferTextureDescriptorSet     = context.viewResources ? context.viewResources->lighting.gBufferTextures.set : DescriptorSetHandle{},
        .shadowDescriptorSet             = context.viewResources ? context.viewResources->lighting.shadows.set : DescriptorSetHandle{},
    };

    graphResources.passes.light = graph.addPass(
        makeViewGraphName(kTopologyPassLight, context.viewId),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(params.frame.handle, params.frame.range);
            passBuilder.uniformRead(params.light.handle, params.light.range);
            for (const auto handle : params.gBufferColors) {
                passBuilder.read(handle);
            }
            passBuilder.read(params.gBufferDepth);
            if (params.ssao.has_value()) {
                passBuilder.read(*params.ssao);
            }
            if (params.shadowDepth.has_value()) {
                passBuilder.read(*params.shadowDepth);
            }
            if (params.environmentCubemap.has_value()) {
                passBuilder.read(*params.environmentCubemap);
            }
            if (params.environmentIrradiance.has_value()) {
                passBuilder.read(*params.environmentIrradiance);
            }
            if (params.environmentPrefilter.has_value()) {
                passBuilder.read(*params.environmentPrefilter);
            }
            if (params.environmentBrdfLut.has_value()) {
                passBuilder.read(*params.environmentBrdfLut);
            }
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stageCtx = context.stageCtx, params, lightStage = &context.lightStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            lightStage->writeGBufferTextureDescriptors(
                params.gBufferTextureDescriptorSet,
                rgCtx.getBindingContext(),
                params.gBufferColors[0],
                params.gBufferColors[1],
                params.gBufferColors[2],
                params.gBufferColors[3],
                params.gBufferDepth,
                params.ssao);

            rgCtx.beginDeclaredRasterRendering();
            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            lightStage->execute(
                stageCtx,
                params.frameAndLightDescriptorSet,
                params.environmentLightingDescriptorSet,
                params.gBufferTextureDescriptorSet,
                params.shadowDescriptorSet);
            rgCtx.endRendering();
        });
}

void appendForwardOpaque(DeferredFrameGraphPassContext& context)
{
    DeferredForwardOpaquePassParams params{
        .color      = context.graphResources.textures.viewColor,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewExtent.toVec2()},
        .layerCount = 1,
    };

    context.graph.addPass(
        makeViewGraphName(kTopologyPassForwardOpaque, context.viewId),
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
        makeViewGraphName(kTopologyPassSkybox, context.viewId),
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

    context.graph.addPass(
        makeViewGraphName(kTopologyPassSprites, context.viewId),
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
        makeViewGraphName(kTopologyPassForwardTransparent, context.viewId),
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

    context.graph.addPass(
        makeViewGraphName("Deferred EntityId", context.viewId),
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
