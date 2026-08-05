#include "DeferredFrameGraphOrchestrator.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Deferred/GBufferStage.h"
#include "Runtime/Rendering/Deferred/LightStage.h"
#include "Runtime/Rendering/Deferred/SSAOStage.h"

#include <format>

namespace ya
{

namespace
{

enum class EDeferredTopologyPass : uint8_t
{
    Shadow,
    GBuffer,
    SSAO,
    Light,
    Skybox,
    SceneOverlay,
    ViewportOverlay,
    Bloom,
    Postprocessing,
};

constexpr std::string_view kTopologyPassShadow          = "Shadow Subgraph";
constexpr std::string_view kTopologyPassGBuffer         = "Deferred GBuffer";
constexpr std::string_view kTopologyPassSSAO            = "SSAO Pass";
constexpr std::string_view kTopologyPassLight           = "Deferred Light";
constexpr std::string_view kTopologyPassSkybox          = "Deferred Skybox";
constexpr std::string_view kTopologyPassSceneOverlay    = "Deferred Scene Overlay";
constexpr std::string_view kTopologyPassViewportOverlay = "Deferred Viewport Overlay";
constexpr std::string_view kTopologyPassBloom           = "Bloom Subgraph";
constexpr std::string_view kTopologyPassPostprocessing  = "Postprocessing";

struct DeferredTopologyEdge
{
    EDeferredTopologyPass from;
    EDeferredTopologyPass to;
};

struct DeferredTopologyPlan
{
    std::vector<EDeferredTopologyPass> order{};
    std::vector<DeferredTopologyEdge>  dependencies{};
};

std::string_view getDeferredTopologyPassName(EDeferredTopologyPass pass)
{
    switch (pass) {
        case EDeferredTopologyPass::Shadow: return kTopologyPassShadow;
        case EDeferredTopologyPass::GBuffer: return kTopologyPassGBuffer;
        case EDeferredTopologyPass::SSAO: return kTopologyPassSSAO;
        case EDeferredTopologyPass::Light: return kTopologyPassLight;
        case EDeferredTopologyPass::Skybox: return kTopologyPassSkybox;
        case EDeferredTopologyPass::SceneOverlay: return kTopologyPassSceneOverlay;
        case EDeferredTopologyPass::ViewportOverlay: return kTopologyPassViewportOverlay;
        case EDeferredTopologyPass::Bloom: return kTopologyPassBloom;
        case EDeferredTopologyPass::Postprocessing: return kTopologyPassPostprocessing;
    }

    YA_CORE_ASSERT(false, "Unhandled deferred topology pass");
    return {};
}

DeferredTopologyPlan buildDeferredTopologyPlan(const DeferredFrameGraphOrchestrator::TopologyInputs& inputs)
{
    DeferredTopologyPlan plan{};
    if (inputs.bHasShadowSubgraph) {
        plan.order.push_back(EDeferredTopologyPass::Shadow);
    }
    plan.order.push_back(EDeferredTopologyPass::GBuffer);
    if (inputs.bUseSSAO) {
        plan.order.push_back(EDeferredTopologyPass::SSAO);
    }
    plan.order.insert(plan.order.end(),
                      {
                          EDeferredTopologyPass::Light,
                          EDeferredTopologyPass::Skybox,
                          EDeferredTopologyPass::SceneOverlay,
                          EDeferredTopologyPass::ViewportOverlay,
                      });
    if (inputs.bHasBloomSubgraph) {
        plan.order.push_back(EDeferredTopologyPass::Bloom);
    }
    if (inputs.bHasPostprocessPass) {
        plan.order.push_back(EDeferredTopologyPass::Postprocessing);
    }

    plan.dependencies.insert(plan.dependencies.end(),
                             {
                                 {EDeferredTopologyPass::GBuffer, EDeferredTopologyPass::Light},
                                 {EDeferredTopologyPass::Light, EDeferredTopologyPass::Skybox},
                                 {EDeferredTopologyPass::Skybox, EDeferredTopologyPass::SceneOverlay},
                                 {EDeferredTopologyPass::SceneOverlay, EDeferredTopologyPass::ViewportOverlay},
                             });
    if (inputs.bHasShadowSubgraph) {
        plan.dependencies.push_back({EDeferredTopologyPass::Shadow, EDeferredTopologyPass::Light});
    }
    if (inputs.bUseSSAO) {
        plan.dependencies.push_back({EDeferredTopologyPass::SSAO, EDeferredTopologyPass::Light});
    }
    if (inputs.bHasBloomSubgraph) {
        plan.dependencies.push_back({EDeferredTopologyPass::ViewportOverlay, EDeferredTopologyPass::Bloom});
        if (inputs.bHasPostprocessPass) {
            plan.dependencies.push_back({EDeferredTopologyPass::Bloom, EDeferredTopologyPass::Postprocessing});
        }
    }
    else if (inputs.bHasPostprocessPass) {
        plan.dependencies.push_back({EDeferredTopologyPass::ViewportOverlay, EDeferredTopologyPass::Postprocessing});
    }

    return plan;
}

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

} // namespace

DeferredFrameGraphOrchestrator::TopologyDescription DeferredFrameGraphOrchestrator::describeTopology(const TopologyInputs& inputs)
{
    const auto plan = buildDeferredTopologyPlan(inputs);
    TopologyDescription topology{};
    topology.passOrder.reserve(plan.order.size());
    for (const auto pass : plan.order) {
        topology.passOrder.push_back(getDeferredTopologyPassName(pass));
    }
    topology.dependencies.reserve(plan.dependencies.size());
    for (const auto& edge : plan.dependencies) {
        topology.dependencies.emplace_back(getDeferredTopologyPassName(edge.from), getDeferredTopologyPassName(edge.to));
    }

    return topology;
}

void DeferredFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
{
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
    const auto& frameBinding   = *inputs.frameBinding;
    const bool  bReverseViewportY = inputs.bReverseViewportY;
    const auto topologyPlan = buildDeferredTopologyPlan({
        .bHasShadowSubgraph  = deps.shadowStage != nullptr,
        .bUseSSAO            = inputs.bUseSSAO,
        .bHasBloomSubgraph   = true,
        .bHasPostprocessPass = true,
    });
    std::optional<RGPassHandle> deferredGBufferPassHandle;
    std::optional<RGPassHandle> deferredSsaoPassHandle;
    std::optional<RGPassHandle> deferredLightPassHandle;
    std::optional<RGPassHandle> deferredSkyboxPassHandle;
    std::optional<RGPassHandle> deferredSceneOverlayPassHandle;
    auto applyTopologyDependencies = [&](RGPassBuilder& passBuilder, EDeferredTopologyPass currentPass)
    {
        for (const auto& edge : topologyPlan.dependencies) {
            if (edge.to != currentPass) {
                continue;
            }
            switch (edge.from) {
                case EDeferredTopologyPass::Shadow:
                    if (graphResources.passes.shadow.lastPass.has_value()) {
                        passBuilder.dependsOn(*graphResources.passes.shadow.lastPass);
                    }
                    break;
                case EDeferredTopologyPass::GBuffer:
                    if (deferredGBufferPassHandle.has_value()) {
                        passBuilder.dependsOn(*deferredGBufferPassHandle);
                    }
                    break;
                case EDeferredTopologyPass::SSAO:
                    break;
                case EDeferredTopologyPass::Light:
                    if (deferredLightPassHandle.has_value()) {
                        passBuilder.dependsOn(*deferredLightPassHandle);
                    }
                    break;
                case EDeferredTopologyPass::Skybox:
                    if (deferredSkyboxPassHandle.has_value()) {
                        passBuilder.dependsOn(*deferredSkyboxPassHandle);
                    }
                    break;
                case EDeferredTopologyPass::SceneOverlay:
                    if (deferredSceneOverlayPassHandle.has_value()) {
                        passBuilder.dependsOn(*deferredSceneOverlayPassHandle);
                    }
                    break;
                default:
                    break;
            }
        }
    };

    if (deps.shadowStage) {
        graphResources.passes.shadow = deps.shadowStage->appendGraphPasses(graph, stageCtx);
    }

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
    const Extent2D gbufferExtent = inputs.gBufferRTSpec->extent;

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
        [&gbufferParams, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EDeferredTopologyPass::GBuffer);
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
        [stageCtx, gbufferParams, gBufferStage = deps.gBufferStage, bReverseViewportY](RGRenderContext& rgCtx) {
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

            gBufferStage->execute(stageCtx, GBufferStage::FrameInputs{
                .frameAndLightDescriptorSet = gbufferParams.frameAndLightDescriptorSet,
                .skinningDescriptorSet      = gbufferParams.skinningDescriptorSet,
            });
            rgCtx.endRendering();
        });

    YA_CORE_ASSERT(!inputs.viewportRTSpec->attachments.colorAttach.empty(), "Deferred viewport graph requires a color attachment spec");
    graphResources.textures.viewportColor = graph.createPersistentTexture(
        makeDeferredOrchestratorGraphAttachmentDesc(*inputs.viewportRTSpec, inputs.viewportRTSpec->attachments.colorAttach.front(), "DeferredViewport.Color"),
        RGPersistentTextureKey{.value = "DeferredViewport.Color"});

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
        [&lightParams, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EDeferredTopologyPass::Light);
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
        [&skyboxParams, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EDeferredTopologyPass::Skybox);
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

    DeferredSceneOverlayPassParams sceneOverlayParams{
        .color      = graphResources.textures.overlayInput,
        .depth      = graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount = 1,
        .overlay    = inputs.overlayInputs ? *inputs.overlayInputs : ViewportOverlayStage::FrameInputs{},
    };

    graphResources.passes.sceneOverlay = graph.addPass(
        std::string(kTopologyPassSceneOverlay),
        [&sceneOverlayParams, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EDeferredTopologyPass::SceneOverlay);
            passBuilder.declareRaster({
                .renderArea = sceneOverlayParams.renderArea,
                .layerCount = sceneOverlayParams.layerCount,
                .colors = {{
                    .color       = sceneOverlayParams.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = sceneOverlayParams.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [stageCtx, sceneOverlayParams, overlayStage = deps.overlayStage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
            overlayStage->executeOverlay(stageCtx, sceneOverlayParams.overlay);

            rgCtx.endRendering();
        });

    DeferredViewportOverlayPassParams viewportOverlayParams{
        .color      = graphResources.textures.overlayInput,
        .depth      = graphResources.textures.gBufferDepth,
        .renderArea = {.pos = {0, 0}, .extent = inputs.viewportExtent.toVec2()},
        .layerCount = 1,
        .recordViewportOverlays = inputs.recordViewportOverlays,
    };

    graphResources.passes.viewportOverlay = graph.addPass(
        std::string(kTopologyPassViewportOverlay),
        [&viewportOverlayParams, &applyTopologyDependencies](RGPassBuilder& passBuilder) {
            applyTopologyDependencies(passBuilder, EDeferredTopologyPass::ViewportOverlay);
            passBuilder.declareRaster({
                .renderArea = viewportOverlayParams.renderArea,
                .layerCount = viewportOverlayParams.layerCount,
                .colors = {{
                    .color       = viewportOverlayParams.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = viewportOverlayParams.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [viewportOverlayParams](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            if (viewportOverlayParams.recordViewportOverlays) {
                YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                viewportOverlayParams.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent);
            }

            rgCtx.endRendering();
        });
    deferredGBufferPassHandle = graphResources.passes.gBuffer;
    deferredLightPassHandle = graphResources.passes.light;
    deferredSkyboxPassHandle = graphResources.passes.skybox;
    deferredSceneOverlayPassHandle = graphResources.passes.sceneOverlay;

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

} // namespace ya
