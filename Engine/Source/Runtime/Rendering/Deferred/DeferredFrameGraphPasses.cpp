#include "DeferredFrameGraphPasses.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"

#include <string>

namespace ya
{

namespace
{

constexpr std::string_view kTopologyPassGBuffer            = "Deferred GBuffer";
constexpr std::string_view kTopologyPassLight             = "Deferred Light";
constexpr std::string_view kTopologyPassForwardOpaque     = "Deferred Forward Opaque";
constexpr std::string_view kTopologyPassSkybox            = "Deferred Skybox";
constexpr std::string_view kTopologyPassForwardTransparent = "Deferred Forward Transparent";
constexpr std::string_view kTopologyPassOverlay           = "Deferred Overlay";

RGImportedTextureDesc makeEnvironmentImportedDesc(const ImageResourceRef& resource,
                                                  std::string_view       label)
{
    return makeImportedTextureDesc(resource, label, EImageLayout::ShaderReadOnlyOptimal);
}

} // namespace

namespace deferred_frame_graph_passes
{

void importFrameBuffers(DeferredFrameGraphPassContext& context)
{
    auto&       graphResources = context.graphResources;
    const auto& frameBinding   = context.frameBinding;

    const auto importHostWritten = [&graphResources, &context](
                                       const stdptr<IBuffer>& buffer,
                                       std::string_view       label,
                                       EBufferUsage           usage,
                                       uint64_t               rangeOffset,
                                       uint64_t               rangeSize) {
        YA_CORE_ASSERT(buffer != nullptr, "Deferred graph requires imported buffer '{}'", label);
        return context.graph.importBuffer(makeHostWrittenImportedBufferDesc(
            buffer,
            label,
            usage,
            rangeOffset,
            rangeSize));
    };

    graphResources.buffers.frame = importHostWritten(
        frameBinding.frame.buffer,
        "Deferred.FrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.frame.offset,
        frameBinding.frame.size);
    graphResources.buffers.light = importHostWritten(
        frameBinding.light.buffer,
        "Deferred.LightUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.light.offset,
        frameBinding.light.size);
    graphResources.buffers.skinning = importHostWritten(
        frameBinding.skinningBuffer,
        "Deferred.SkinningSSBO",
        EBufferUsage::StorageBuffer,
        0,
        0);
    if (context.bUseSSAO) {
        graphResources.buffers.ssaoFrame = importHostWritten(
            frameBinding.ssaoFrame.buffer,
            "Deferred.SSAOFrameUBO",
            EBufferUsage::UniformBuffer,
            frameBinding.ssaoFrame.offset,
            frameBinding.ssaoFrame.size);
    }
    graphResources.buffers.skyboxFrame = importHostWritten(
        frameBinding.skyboxFrame.buffer,
        "Deferred.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.skyboxFrame.offset,
        frameBinding.skyboxFrame.size);
}

void createAttachmentTextures(DeferredFrameGraphPassContext& context)
{
    auto&       graphResources = context.graphResources;
    const auto& gBufferSpec    = context.gBufferRTSpec;
    const auto& viewportSpec   = context.viewportRTSpec;

    const auto makeAttachmentDesc = [](const RenderTargetCreateInfo& spec,
                                       const AttachmentDescription&  attachment,
                                       std::string                   label) {
        return RGTextureDesc{
            .label       = std::move(label),
            .format      = attachment.format,
            .extent      = Extent3D{spec.extent.width, spec.extent.height, 1},
            .mipLevels   = 1,
            .arrayLayers = spec.layerCount,
            .samples     = attachment.samples,
            .usage       = attachment.usage,
            .flags       = attachment.imageCreateFlags,
        };
    };

    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        graphResources.textures.gBufferColors[attachmentIndex] = context.graph.createPersistentTexture(
            makeAttachmentDesc(
                gBufferSpec,
                gBufferSpec.attachments.colorAttach[attachmentIndex],
                std::format("DeferredGBuffer.Color{}", attachmentIndex)),
            RGPersistentTextureKey{.value = std::format("DeferredGBuffer.Color{}", attachmentIndex)});
    }
    YA_CORE_ASSERT(gBufferSpec.attachments.depthAttach.has_value(),
                   "Deferred GBuffer graph requires a depth attachment spec");
    graphResources.textures.gBufferDepth = context.graph.createPersistentTexture(
        makeAttachmentDesc(gBufferSpec, *gBufferSpec.attachments.depthAttach, "DeferredGBuffer.Depth"),
        RGPersistentTextureKey{.value = "DeferredGBuffer.Depth"});

    YA_CORE_ASSERT(!viewportSpec.attachments.colorAttach.empty(),
                   "Deferred viewport graph requires a color attachment spec");
    graphResources.textures.viewportColor = context.graph.createPersistentTexture(
        makeAttachmentDesc(viewportSpec, viewportSpec.attachments.colorAttach.front(), "DeferredViewport.Color"),
        RGPersistentTextureKey{.value = "DeferredViewport.Color"});

    AttachmentDescription entityIdDesc{};
    entityIdDesc.format      = EFormat::R32_UINT;
    entityIdDesc.samples     = ESampleCount::Sample_1;
    entityIdDesc.loadOp      = EAttachmentLoadOp::Clear;
    entityIdDesc.storeOp     = EAttachmentStoreOp::Store;
    entityIdDesc.usage       = EImageUsage::ColorAttachment | EImageUsage::TransferSrc;
    entityIdDesc.finalLayout = EImageLayout::ColorAttachmentOptimal;
    graphResources.textures.entityId = context.graph.createPersistentTexture(
        makeAttachmentDesc(viewportSpec, entityIdDesc, "DeferredViewport.EntityId"),
        RGPersistentTextureKey{.value = "DeferredViewport.EntityId"});
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
        std::string(kTopologyPassGBuffer),
        [&params](RGPassBuilder& passBuilder) {
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
        });
}

void appendLight(DeferredFrameGraphPassContext& context)
{
    auto&       graph          = context.graph;
    auto&       graphResources = context.graphResources;
    const auto& frameBinding  = context.frameBinding;

    if (context.environmentLighting && context.environmentLighting->isComplete()) {
        graphResources.textures.environmentCubemap = graph.importTexture(
            makeEnvironmentImportedDesc(context.environmentLighting->cubemap,
                                         "DeferredLight.Environment.Cubemap"));
        graphResources.textures.environmentIrradiance = graph.importTexture(
            makeEnvironmentImportedDesc(context.environmentLighting->irradiance,
                                         "DeferredLight.Environment.Irradiance"));
        graphResources.textures.environmentPrefilter = graph.importTexture(
            makeEnvironmentImportedDesc(context.environmentLighting->prefilter,
                                         "DeferredLight.Environment.Prefilter"));
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
        .viewportColor                    = graphResources.textures.viewportColor,
        .renderArea                      = Rect2D{.pos = {0, 0}, .extent = context.viewportExtent.toVec2()},
        .layerCount                      = 1,
        .frameAndLightDescriptorSet      = frameBinding.frameAndLightDescriptorSet,
        .environmentLightingDescriptorSet = context.environmentLightingDS,
    };

    graphResources.passes.light = graph.addPass(
        std::string(kTopologyPassLight),
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
                    .color       = params.viewportColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stageCtx = context.stageCtx, params, lightStage = &context.lightStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            lightStage->updateGBufferTextureDescriptors(
                rgCtx.getBindingContext(),
                params.gBufferColors[0],
                params.gBufferColors[1],
                params.gBufferColors[2],
                params.gBufferColors[3],
                params.gBufferDepth,
                params.ssao);

            rgCtx.beginDeclaredRasterRendering();
            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            lightStage->execute(stageCtx, params.frameAndLightDescriptorSet, params.environmentLightingDescriptorSet);
            rgCtx.endRendering();
        });
}

void appendForwardOpaque(DeferredFrameGraphPassContext& context)
{
    DeferredForwardOpaquePassParams params{
        .color      = context.graphResources.textures.viewportColor,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewportExtent.toVec2()},
        .layerCount = 1,
    };

    context.graph.addPass(
        std::string(kTopologyPassForwardOpaque),
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
        .viewportColor = context.graphResources.textures.viewportColor,
        .depth         = context.graphResources.textures.gBufferDepth,
        .renderArea    = {.pos = {0, 0}, .extent = context.viewportExtent.toVec2()},
        .layerCount    = 1,
        .skybox        = context.overlayInputs
            ? context.overlayInputs->skybox
            : ViewportOverlayStage::FrameInputs::SkyboxInput{},
    };

    context.graphResources.passes.skybox = context.graph.addPass(
        std::string(kTopologyPassSkybox),
        [&params](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(params.frame.handle, params.frame.range);
            passBuilder.declareRaster({
                .renderArea = params.renderArea,
                .layerCount = params.layerCount,
                .colors = {{
                    .color       = params.viewportColor,
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

void appendBloom(DeferredFrameGraphPassContext& context)
{
    const auto bloomComposite = context.postProcessStage.appendBloomGraphPasses(
        context.graph,
        context.graphResources.textures.viewportColor,
        context.viewportExtent,
        context.postContext);
    if (bloomComposite.isValid()) {
        context.graphResources.textures.bloomComposite = bloomComposite;
    }
    context.graphResources.textures.overlayInput = bloomComposite.isValid()
        ? bloomComposite
        : context.graphResources.textures.viewportColor;
}

void appendForwardTransparent(DeferredFrameGraphPassContext& context)
{
    DeferredForwardTransparentPassParams params{
        .color      = context.graphResources.textures.overlayInput,
        .depth      = context.graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = context.viewportExtent.toVec2()},
        .layerCount = 1,
        .overlay    = context.overlayInputs
            ? *context.overlayInputs
            : ViewportOverlayStage::FrameInputs{},
    };

    context.graphResources.passes.sceneOverlay = context.graph.addPass(
        std::string(kTopologyPassForwardTransparent),
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
            overlayStage->executeOverlay(stageCtx, params.overlay);
            rgCtx.endRendering();
        });
}

void appendEntityId(DeferredFrameGraphPassContext& context)
{
    const auto entityId = context.graphResources.textures.entityId;
    const auto depth    = context.graphResources.textures.gBufferDepth;
    const auto extent   = context.viewportExtent;
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
        "Deferred EntityId",
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
        [stageCtx, entityIdPass, frameBinding, billboards = std::move(billboards)](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            if (entityIdPass && stageCtx.frameData) {
                entityIdPass->execute(&rgCtx.getCommandBuffer(),
                                      viewportExtent.width,
                                      viewportExtent.height,
                                      stageCtx.frameData->projection * stageCtx.frameData->view,
                                      stageCtx.frameData->view,
                                      *stageCtx.frameData,
                                      frameBinding.skinningDescriptorSet,
                                      billboards);
            }
            rgCtx.endRendering();
        });
}

void appendOverlay(DeferredFrameGraphPassContext& context)
{
    DeferredOverlayPassParams params{
        .color          = context.graphResources.textures.overlayInput,
        .depth          = context.graphResources.textures.gBufferDepth,
        .renderArea     = {.pos = {0, 0}, .extent = context.viewportExtent.toVec2()},
        .layerCount     = 1,
        .overlaySnapshot = context.viewportOverlaySnapshot,
        .frameCtx       = context.postContext ? *context.postContext : FrameContext{},
    };

    context.graphResources.passes.viewportOverlay = context.graph.addPass(
        std::string(kTopologyPassOverlay),
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
        [params](RGRenderContext& rgCtx) mutable {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            params.frameCtx.extent = viewportExtent;
            recordRenderViewportOverlayPass(
                params.frameCtx,
                params.overlaySnapshot,
                &rgCtx.getCommandBuffer());
            rgCtx.endRendering();
        });
}

void appendUI(DeferredFrameGraphPassContext& context)
{
    if (!context.uiSceneRoot) {
        return;
    }

    const auto color  = context.graphResources.textures.overlayInput;
    const auto depth  = context.graphResources.textures.gBufferDepth;
    const auto extent = context.viewportExtent;
    const auto uiRoot = context.uiSceneRoot;

    context.graph.addPass(
        "Deferred UI",
        [color, depth, extent](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = extent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [uiRoot](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            recordRenderUIPass(uiRoot, &rgCtx.getCommandBuffer(), viewportExtent);
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
            .inputExtent   = context.viewportExtent,
            .bOutputIsSRGB = context.bPostprocessOutputIsSRGB,
            .postContext   = context.postContext,
        });
    if (postprocessOutput.isValid()) {
        context.graphResources.textures.postprocessOutput = postprocessOutput;
    }
}

} // namespace deferred_frame_graph_passes

} // namespace ya
