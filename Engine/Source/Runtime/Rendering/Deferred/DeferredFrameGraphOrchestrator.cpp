#include "DeferredFrameGraphOrchestrator.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Runtime/Rendering/Common/EntityIdViewportPass.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Deferred/GBufferStage.h"
#include "Runtime/Rendering/Deferred/LightStage.h"
#include "Runtime/Rendering/Deferred/SSAOStage.h"

#include <format>

namespace ya
{

namespace
{

constexpr std::string_view kTopologyPassShadow            = "Shadow Subgraph";
constexpr std::string_view kTopologyPassGBuffer           = "Deferred GBuffer";
constexpr std::string_view kTopologyPassSSAO              = "SSAO Pass";
constexpr std::string_view kTopologyPassLight             = "Deferred Light";
constexpr std::string_view kTopologyPassForwardOpaque     = "Deferred Forward Opaque";
constexpr std::string_view kTopologyPassSkybox            = "Deferred Skybox";
constexpr std::string_view kTopologyPassForwardTransparent = "Deferred Forward Transparent";
constexpr std::string_view kTopologyPassOverlay           = "Deferred Overlay";
constexpr std::string_view kTopologyPassBloom             = "Bloom Subgraph";
constexpr std::string_view kTopologyPassPostprocessing    = "Postprocessing";

RGImportedTextureDesc makeDeferredOrchestratorEnvironmentImportedDesc(const ImageResourceRef& resource,
                                                                      std::string_view       label)
{
    return makeImportedTextureDesc(resource, label, EImageLayout::ShaderReadOnlyOptimal);
}

RGTextureDesc makeDeferredOrchestratorGraphAttachmentDesc(const RenderTargetCreateInfo& spec,
                                                          const AttachmentDescription&  attachment,
                                                          std::string                   label)
{
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
}

RGBufferHandle importDeferredOrchestratorHostWrittenBuffer(RenderGraph&           graph,
                                                           const stdptr<IBuffer>& buffer,
                                                           std::string            label,
                                                           EBufferUsage           usage,
                                                           uint64_t               rangeOffset = 0,
                                                           uint64_t               rangeSize   = 0)
{
    YA_CORE_ASSERT(buffer != nullptr, "Deferred graph requires imported buffer '{}'", label);
    return graph.importBuffer(makeHostWrittenImportedBufferDesc(
        buffer,
        label,
        usage,
        rangeOffset,
        rangeSize));
}

void importDeferredFrameBuffers(RenderGraph&                                   graph,
                                DeferredFrameGraphResources&                   graphResources,
                                const DeferredFrameGraphOrchestrator::BuildInputs& inputs)
{
    const auto& frameBinding = *inputs.frameBinding;
    graphResources.buffers.frame = importDeferredOrchestratorHostWrittenBuffer(
        graph,
        frameBinding.frame.buffer,
        "Deferred.FrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.frame.offset,
        frameBinding.frame.size);
    graphResources.buffers.light = importDeferredOrchestratorHostWrittenBuffer(
        graph,
        frameBinding.light.buffer,
        "Deferred.LightUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.light.offset,
        frameBinding.light.size);
    graphResources.buffers.skinning = importDeferredOrchestratorHostWrittenBuffer(
        graph,
        frameBinding.skinningBuffer,
        "Deferred.SkinningSSBO",
        EBufferUsage::StorageBuffer);
    if (inputs.bUseSSAO) {
        graphResources.buffers.ssaoFrame = importDeferredOrchestratorHostWrittenBuffer(
            graph,
            frameBinding.ssaoFrame.buffer,
            "Deferred.SSAOFrameUBO",
            EBufferUsage::UniformBuffer,
            frameBinding.ssaoFrame.offset,
            frameBinding.ssaoFrame.size);
    }
    graphResources.buffers.skyboxFrame = importDeferredOrchestratorHostWrittenBuffer(
        graph,
        frameBinding.skyboxFrame.buffer,
        "Deferred.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.skyboxFrame.offset,
        frameBinding.skyboxFrame.size);
}

void createDeferredAttachmentTextures(RenderGraph&                                   graph,
                                      DeferredFrameGraphResources&                   graphResources,
                                      const DeferredFrameGraphOrchestrator::BuildInputs& inputs)
{
    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        graphResources.textures.gBufferColors[attachmentIndex] = graph.createPersistentTexture(
            makeDeferredOrchestratorGraphAttachmentDesc(
                *inputs.gBufferRTSpec,
                inputs.gBufferRTSpec->attachments.colorAttach[attachmentIndex],
                std::format("DeferredGBuffer.Color{}", attachmentIndex)),
            RGPersistentTextureKey{.value = std::format("DeferredGBuffer.Color{}", attachmentIndex)});
    }
    YA_CORE_ASSERT(inputs.gBufferRTSpec->attachments.depthAttach.has_value(), "Deferred GBuffer graph requires a depth attachment spec");
    graphResources.textures.gBufferDepth = graph.createPersistentTexture(
        makeDeferredOrchestratorGraphAttachmentDesc(*inputs.gBufferRTSpec, *inputs.gBufferRTSpec->attachments.depthAttach, "DeferredGBuffer.Depth"),
        RGPersistentTextureKey{.value = "DeferredGBuffer.Depth"});

    YA_CORE_ASSERT(!inputs.viewportRTSpec->attachments.colorAttach.empty(), "Deferred viewport graph requires a color attachment spec");
    graphResources.textures.viewportColor = graph.createPersistentTexture(
        makeDeferredOrchestratorGraphAttachmentDesc(*inputs.viewportRTSpec, inputs.viewportRTSpec->attachments.colorAttach.front(), "DeferredViewport.Color"),
        RGPersistentTextureKey{.value = "DeferredViewport.Color"});

    AttachmentDescription entityIdDesc{};
    entityIdDesc.format      = EFormat::R32_UINT;
    entityIdDesc.samples     = ESampleCount::Sample_1;
    entityIdDesc.loadOp      = EAttachmentLoadOp::Clear;
    entityIdDesc.storeOp     = EAttachmentStoreOp::Store;
    entityIdDesc.usage       = EImageUsage::ColorAttachment | EImageUsage::TransferSrc;
    entityIdDesc.finalLayout = EImageLayout::ColorAttachmentOptimal;
    graphResources.textures.entityId = graph.createPersistentTexture(
        makeDeferredOrchestratorGraphAttachmentDesc(*inputs.viewportRTSpec, entityIdDesc, "DeferredViewport.EntityId"),
        RGPersistentTextureKey{.value = "DeferredViewport.EntityId"});
}

void appendDeferredGBufferPass(RenderGraph&                                   graph,
                               DeferredFrameGraphResources&                   graphResources,
                               const DeferredFrameGraphOrchestrator::BuildInputs& inputs,
                               GBufferStage&                                 gBufferStage,
                               bool                                          bReverseViewportY)
{
    const auto& frameBinding  = *inputs.frameBinding;
    const auto& stageCtx      = *inputs.stageCtx;
    const auto  gbufferExtent = inputs.gBufferRTSpec->extent;

    DeferredGBufferPassParams gbufferParams{
        .frame = {
            .handle = graphResources.buffers.frame,
            .range  = RGBufferRange{.offset = frameBinding.frame.offset, .size = frameBinding.frame.size},
        },
        .light = {
            .handle = graphResources.buffers.light,
            .range  = RGBufferRange{.offset = frameBinding.light.offset, .size = frameBinding.light.size},
        },
        .skinning = graphResources.buffers.skinning,
        .gBufferColors = graphResources.textures.gBufferColors,
        .gBufferDepth  = graphResources.textures.gBufferDepth,
        .renderArea    = Rect2D{.pos = {0, 0}, .extent = gbufferExtent.toVec2()},
        .layerCount    = 1,
        .frameAndLightDescriptorSet = frameBinding.frameAndLightDescriptorSet,
        .skinningDescriptorSet      = frameBinding.skinningDescriptorSet,
    };

    graphResources.passes.gBuffer = graph.addPass(
        std::string(kTopologyPassGBuffer),
        [&gbufferParams](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(gbufferParams.frame.handle, gbufferParams.frame.range);
            passBuilder.uniformRead(gbufferParams.light.handle, gbufferParams.light.range);
            passBuilder.storageRead(gbufferParams.skinning);
            passBuilder.declareRaster({
                .renderArea = gbufferParams.renderArea,
                .layerCount = gbufferParams.layerCount,
                .colors = {
                    {.color = gbufferParams.gBufferColors[0], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferParams.gBufferColors[1], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferParams.gBufferColors[2], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferParams.gBufferColors[3], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGDepthAttachmentDesc{
                    .depth       = gbufferParams.gBufferDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, gbufferParams, &gBufferStage, bReverseViewportY](RGRenderContext& rgCtx) {
            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            [[maybe_unused]] IBuffer* const frameBuffer    = rgCtx.resolveBuffer(gbufferParams.frame.handle);
            [[maybe_unused]] IBuffer* const lightBuffer    = rgCtx.resolveBuffer(gbufferParams.light.handle);
            [[maybe_unused]] IBuffer* const skinningBuffer = rgCtx.resolveBuffer(gbufferParams.skinning);

            rgCtx.beginDeclaredRasterRendering();

            float gbVpY = 0.0f;
            const auto gbufferExtent = rasterParams.getRenderExtent();
            const auto vpW           = gbufferExtent.width;
            const auto vpH           = gbufferExtent.height;
            float gbVpH = static_cast<float>(vpH);
            if (bReverseViewportY) {
                gbVpY = static_cast<float>(vpH);
                gbVpH = -gbVpH;
            }
            rgCtx.getCommandBuffer().setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
            rgCtx.getCommandBuffer().setScissor(0, 0, vpW, vpH);

            gBufferStage.execute(stageCtx, GBufferStage::FrameInputs{
                .frameAndLightDescriptorSet = gbufferParams.frameAndLightDescriptorSet,
                .skinningDescriptorSet      = gbufferParams.skinningDescriptorSet,
            });
            rgCtx.endRendering();
        });
}

void appendDeferredLightingAndPostprocess(RenderGraph&                                   graph,
                                          DeferredFrameGraphResources&                   graphResources,
                                          const DeferredFrameGraphOrchestrator::BuildInputs& inputs,
                                          const DeferredFrameGraphOrchestrator::BuildDependencies& deps)
{
    const auto& frameBinding = *inputs.frameBinding;
    const auto& stageCtx     = *inputs.stageCtx;

    if (inputs.bUseSSAO) {
        YA_CORE_ASSERT(graphResources.buffers.ssaoFrame.has_value(), "Deferred SSAO requires an imported frame buffer");
        YA_CORE_ASSERT(deps.ssaoStage != nullptr, "Deferred SSAO requires an SSAO stage");
        graphResources.textures.ssao = deps.ssaoStage->appendGraphPass(
            graph,
            stageCtx,
            DeferredSSAOPassParams{
                .frame      = *graphResources.buffers.ssaoFrame,
                .frameRange = RGBufferRange{.offset = frameBinding.ssaoFrame.offset, .size = frameBinding.ssaoFrame.size},
                .albedo     = graphResources.textures.gBufferColors[0],
                .normal     = graphResources.textures.gBufferColors[1],
                .depth      = graphResources.textures.gBufferDepth,
                .frameDescriptorSet = frameBinding.ssaoFrameDescriptorSet,
            });
    }

    if (inputs.environmentLighting && inputs.environmentLighting->isComplete()) {
        graphResources.textures.environmentCubemap = graph.importTexture(
            makeDeferredOrchestratorEnvironmentImportedDesc(inputs.environmentLighting->cubemap,
                                                            "DeferredLight.Environment.Cubemap"));
        graphResources.textures.environmentIrradiance = graph.importTexture(
            makeDeferredOrchestratorEnvironmentImportedDesc(inputs.environmentLighting->irradiance,
                                                            "DeferredLight.Environment.Irradiance"));
        graphResources.textures.environmentPrefilter = graph.importTexture(
            makeDeferredOrchestratorEnvironmentImportedDesc(inputs.environmentLighting->prefilter,
                                                            "DeferredLight.Environment.Prefilter"));
        graphResources.textures.environmentBrdfLut = graph.importTexture(
            makeImportedTextureDesc(*inputs.environmentLighting->brdfLut,
                                    "DeferredLight.Environment.BrdfLut",
                                    EImageLayout::ShaderReadOnlyOptimal));
    }

    graphResources.textures.shadowDepth = graphResources.passes.shadow.shadowDepth;

    DeferredLightPassParams lightParams{
        .frame = {
            .handle = graphResources.buffers.frame,
            .range  = RGBufferRange{.offset = frameBinding.frame.offset, .size = frameBinding.frame.size},
        },
        .light = {
            .handle = graphResources.buffers.light,
            .range  = RGBufferRange{.offset = frameBinding.light.offset, .size = frameBinding.light.size},
        },
        .gBufferColors = graphResources.textures.gBufferColors,
        .gBufferDepth  = graphResources.textures.gBufferDepth,
        .ssao          = graphResources.textures.ssao,
        .environmentCubemap    = graphResources.textures.environmentCubemap,
        .environmentIrradiance = graphResources.textures.environmentIrradiance,
        .environmentPrefilter  = graphResources.textures.environmentPrefilter,
        .environmentBrdfLut    = graphResources.textures.environmentBrdfLut,
        .shadowDepth           = graphResources.textures.shadowDepth,
        .viewportColor         = graphResources.textures.viewportColor,
        .renderArea            = Rect2D{.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount            = 1,
        .frameAndLightDescriptorSet = frameBinding.frameAndLightDescriptorSet,
        .environmentLightingDescriptorSet = inputs.environmentLightingDS,
    };

    graphResources.passes.light = graph.addPass(
        std::string(kTopologyPassLight),
        [&lightParams](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(lightParams.frame.handle, lightParams.frame.range);
            passBuilder.uniformRead(lightParams.light.handle, lightParams.light.range);
            for (const auto handle : lightParams.gBufferColors) {
                passBuilder.read(handle);
            }
            passBuilder.read(lightParams.gBufferDepth);
            if (lightParams.ssao.has_value()) {
                passBuilder.read(*lightParams.ssao);
            }
            if (lightParams.shadowDepth.has_value()) {
                passBuilder.read(*lightParams.shadowDepth);
            }
            if (lightParams.environmentCubemap.has_value()) {
                passBuilder.read(*lightParams.environmentCubemap);
            }
            if (lightParams.environmentIrradiance.has_value()) {
                passBuilder.read(*lightParams.environmentIrradiance);
            }
            if (lightParams.environmentPrefilter.has_value()) {
                passBuilder.read(*lightParams.environmentPrefilter);
            }
            if (lightParams.environmentBrdfLut.has_value()) {
                passBuilder.read(*lightParams.environmentBrdfLut);
            }
            passBuilder.declareRaster({
                .renderArea = lightParams.renderArea,
                .layerCount = lightParams.layerCount,
                .colors = {{
                    .color       = lightParams.viewportColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stageCtx, lightParams, lightStage = deps.lightStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            lightStage->updateGBufferTextureDescriptors(
                rgCtx.getBindingContext(),
                lightParams.gBufferColors[0],
                lightParams.gBufferColors[1],
                lightParams.gBufferColors[2],
                lightParams.gBufferColors[3],
                lightParams.gBufferDepth,
                lightParams.ssao);

            rgCtx.beginDeclaredRasterRendering();

            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            lightStage->execute(stageCtx, lightParams.frameAndLightDescriptorSet, lightParams.environmentLightingDescriptorSet);
            rgCtx.endRendering();
        });

    DeferredForwardOpaquePassParams forwardOpaqueParams{
        .color      = graphResources.textures.viewportColor,
        .depth      = graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount = 1,
    };

    [[maybe_unused]] const auto forwardOpaquePass = graph.addPass(
        std::string(kTopologyPassForwardOpaque),
        [&forwardOpaqueParams](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = forwardOpaqueParams.renderArea,
                .layerCount = forwardOpaqueParams.layerCount,
                .colors = {{
                    .color       = forwardOpaqueParams.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = forwardOpaqueParams.depth,
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

    DeferredSkyboxPassParams skyboxParams{
        .frame = {
            .handle = graphResources.buffers.skyboxFrame,
            .range  = RGBufferRange{
                .offset = frameBinding.skyboxFrame.offset,
                .size   = frameBinding.skyboxFrame.size,
            },
        },
        .viewportColor = graphResources.textures.viewportColor,
        .depth         = graphResources.textures.gBufferDepth,
        .renderArea    = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount    = 1,
        .skybox        = inputs.overlayInputs ? inputs.overlayInputs->skybox : ViewportOverlayStage::FrameInputs::SkyboxInput{},
    };

    graphResources.passes.skybox = graph.addPass(
        std::string(kTopologyPassSkybox),
        [&skyboxParams](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(skyboxParams.frame.handle, skyboxParams.frame.range);
            passBuilder.declareRaster({
                .renderArea = skyboxParams.renderArea,
                .layerCount = skyboxParams.layerCount,
                .colors = {{
                    .color       = skyboxParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = skyboxParams.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, skyboxParams, overlayStage = deps.overlayStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            overlayStage->executeSkybox(stageCtx, skyboxParams.skybox);
            rgCtx.endRendering();
        });

    const auto bloomComposite = deps.postProcessStage->appendBloomGraphPasses(
        graph,
        graphResources.textures.viewportColor,
        inputs.viewportExtent,
        inputs.postContext);
    if (bloomComposite.isValid()) {
        graphResources.textures.bloomComposite = bloomComposite;
    }
    graphResources.textures.overlayInput = bloomComposite.isValid() ? bloomComposite : graphResources.textures.viewportColor;

    DeferredForwardTransparentPassParams forwardTransparentParams{
        .color      = graphResources.textures.overlayInput,
        .depth      = graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount = 1,
        .overlay    = inputs.overlayInputs ? *inputs.overlayInputs : ViewportOverlayStage::FrameInputs{},
    };

    graphResources.passes.sceneOverlay = graph.addPass(
        std::string(kTopologyPassForwardTransparent),
        [&forwardTransparentParams](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = forwardTransparentParams.renderArea,
                .layerCount = forwardTransparentParams.layerCount,
                .colors = {{
                    .color       = forwardTransparentParams.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = forwardTransparentParams.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, forwardTransparentParams, overlayStage = deps.overlayStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
            overlayStage->executeOverlay(stageCtx, forwardTransparentParams.overlay);

            rgCtx.endRendering();
        });

    // Entity-id pick pass: write every draw item's entity id, depth-tested
    // against the viewport (gBuffer) depth so ids match what is visible.
    [[maybe_unused]] const auto entityIdPassHandle = graph.addPass(
        std::string("Deferred EntityId"),
        [&entityIdHandle = graphResources.textures.entityId, &depthHandle = graphResources.textures.gBufferDepth, viewportExtent = inputs.viewportExtent](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = entityIdHandle,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ColorAttachmentOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depthHandle,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, entityIdPass = deps.entityIdPass, frameBinding](RGRenderContext& rgCtx) {
            const auto viewportExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            if (entityIdPass && stageCtx.frameData) {
                entityIdPass->execute(&rgCtx.getCommandBuffer(),
                                      viewportExtent.width,
                                      viewportExtent.height,
                                      stageCtx.frameData->projection * stageCtx.frameData->view,
                                      stageCtx.frameData->view,
                                      *stageCtx.frameData,
                                      frameBinding.skinningDescriptorSet);
            }
            rgCtx.endRendering();
        });

    DeferredOverlayPassParams overlayParams{
        .color          = graphResources.textures.overlayInput,
        .depth          = graphResources.textures.gBufferDepth,
        .renderArea     = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount     = 1,
        .overlaySnapshot = inputs.viewportOverlaySnapshot,
        .frameCtx       = inputs.postContext ? *inputs.postContext : FrameContext{},
    };

    graphResources.passes.viewportOverlay = graph.addPass(
        std::string(kTopologyPassOverlay),
        [&overlayParams](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = overlayParams.renderArea,
                .layerCount = overlayParams.layerCount,
                .colors = {{
                    .color       = overlayParams.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = overlayParams.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [overlayParams](RGRenderContext& rgCtx) mutable {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            overlayParams.frameCtx.extent = viewportExtent;
            recordRenderViewportOverlayPass(overlayParams.frameCtx,
                                            overlayParams.overlaySnapshot,
                                            &rgCtx.getCommandBuffer());
            rgCtx.endRendering();
        });
    const auto postprocessOutput = deps.postProcessStage->appendFinalizeGraphPasses(graph, PostProcessingStage::FinalizePassParams{
        .input         = graphResources.textures.overlayInput,
        .output        = graphResources.textures.postprocessOutput.value_or(RGTextureHandle{}),
        .inputExtent   = inputs.viewportExtent,
        .bOutputIsSRGB = inputs.bPostprocessOutputIsSRGB,
        .postContext   = inputs.postContext,
    });
    if (postprocessOutput.isValid()) {
        graphResources.textures.postprocessOutput = postprocessOutput;
    }
}

} // namespace

void DeferredFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
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
    const bool  bReverseViewportY = inputs.bReverseViewportY;
    if (deps.shadowStage) {
        graphResources.passes.shadow = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }
    importDeferredFrameBuffers(graph, graphResources, inputs);
    createDeferredAttachmentTextures(graph, graphResources, inputs);
    appendDeferredGBufferPass(graph, graphResources, inputs, *deps.gBufferStage, bReverseViewportY);
    appendDeferredLightingAndPostprocess(graph, graphResources, inputs, deps);

    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        graph.exportTexture(graphResources.textures.gBufferColors[attachmentIndex], std::string(deferred_graph_exports::gBufferColor[attachmentIndex]));
    }
    graph.exportTexture(graphResources.textures.gBufferDepth, std::string(deferred_graph_exports::gBufferDepth));
    graph.exportTexture(graphResources.textures.viewportColor, std::string(deferred_graph_exports::viewportColor));
    graph.exportTexture(graphResources.textures.entityId, std::string(deferred_graph_exports::entityId));
    if (graphResources.textures.ssao.has_value()) {
        graph.exportTexture(*graphResources.textures.ssao, std::string(deferred_graph_exports::ssao));
    }
}

} // namespace ya
