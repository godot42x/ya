#include "ForwardViewportStage.h"

#include "Core/Math/Math.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Scene/Scene.h"

#include "glm/gtc/type_ptr.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Common vertex attributes
// ═══════════════════════════════════════════════════════════════════════

static const std::vector<VertexAttribute> kVertexAttributes3 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
};

static const std::vector<VertexAttribute> kSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

static const VertexBufferDescription kVBDesc{.slot = 0, .pitch = sizeof(ya::Vertex)};

void ForwardViewportStage::initSkinningResources()
{
    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Forward_Skinning_DSL",
            .set      = 5,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
}

void ForwardViewportStage::ensureSkinningCapacity(uint32_t paletteCount)
{
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (_skinningDSP && requiredCount <= _skinningCapacity) {
        return;
    }

    _skinningDSP.reset();
    for (auto& ds : _skinningDS) {
        ds = {};
    }

    _skinningCapacity = std::max(requiredCount, _skinningCapacity == 0 ? 16u : _skinningCapacity);
    while (_skinningCapacity < requiredCount) {
        _skinningCapacity *= 2;
    }

    _skinningDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Forward_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    const uint32_t bufferSize = static_cast<uint32_t>(static_cast<uint64_t>(_skinningCapacity) * sizeof(RenderSkinningPalette));
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skinningSSBO[i] = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("Forward_Skinning_SSBO_{}", i),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        _skinningDS[i] = _skinningDSP->allocateDescriptorSets(_skinningDSL);
        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::genSingleBufferWrite(_skinningDS[i], 0, EPipelineDescriptorType::StorageBuffer, _skinningSSBO[i].get())},
            {});
    }
}

void ForwardViewportStage::updateSkinningBuffer(const RenderStageContext& ctx)
{
    if (!ctx.frameData) {
        return;
    }

    const auto& palettes = ctx.frameData->skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (palettes.empty()) {
        return;
    }

    auto& buffer = _skinningSSBO[ctx.flightIndex];
    buffer->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
    buffer->flush();
}

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::init(IRender* render)
{
    // Stub — use initWithDesc() instead.
    _render = render;
}

void ForwardViewportStage::initWithDesc(const InitDesc& desc)
{
    _render                                   = desc.render;
    _depthBufferShadowDS                      = desc.depthBufferShadowDS;
    _shadowState                              = desc.shadowState;
    _getFrameIndex                            = desc.getFrameIndex;
    _getElapsedTimeSeconds                    = desc.getElapsedTimeSeconds;
    _getActiveScene                           = desc.getActiveScene;
    _getResourceResolveSystem                 = desc.getResourceResolveSystem;
    _getSceneSkyboxDescriptorSet              = desc.getSceneSkyboxDescriptorSet;
    _getSceneEnvironmentLightingDescriptorSet = desc.getSceneEnvironmentLightingDescriptorSet;

    initSkinningResources();
    _litPasses.init(ForwardViewportLitPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .shadowState = desc.shadowState,
        .skinningDSL = _skinningDSL,
        .getFrameIndex = desc.getFrameIndex,
        .getElapsedTimeSeconds = desc.getElapsedTimeSeconds,
    });
    _unlitPass.init(ForwardViewportUnlitPass::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .skinningDSL = _skinningDSL,
        .getFrameIndex = desc.getFrameIndex,
        .getElapsedTimeSeconds = desc.getElapsedTimeSeconds,
    });
    initSimple(desc);
    initSkybox(desc);
    initDebug(desc);
}

void ForwardViewportStage::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    if (!viewportRT) {
        return;
    }

    const auto& colorDescs = viewportRT->getColorAttachmentDescs();
    const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
    if (colorDescs.empty()) {
        return;
    }

    const auto colorFormat = colorDescs.front().format;
    const auto depthFormat = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;

    _litPasses.refreshPipelineFormats(viewportRT);

    _unlitPass.refreshPipelineFormats(viewportRT);

    if (_simplePipeline) {
        auto ci                                         = _simplePipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _simplePipeline->updateDesc(std::move(ci));
    }

    if (_skyboxPipeline) {
        auto ci                                         = _skyboxPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _skyboxPipeline->updateDesc(std::move(ci));
    }

    if (_debugPipeline) {
        _debugPipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        _debugPipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _debugPipeline->updateDesc(_debugPipelineCI);
    }
}

// ── Simple ──────────────────────────────────────────────────────────
void ForwardViewportStage::initSimple(const InitDesc& desc)
{
    _simplePPL = IPipelineLayout::create(
        _render, "FwdSimple_PPL", {PushConstantRange{.offset = 0, .size = sizeof(SimplePC), .stageFlags = EShaderStage::Vertex}}, {});

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _simplePPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/SimpleMaterial.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _simplePipeline = IGraphicsPipeline::create(_render);
    _simplePipeline->recreate(ci);
}

// ── Skybox ──────────────────────────────────────────────────────────
void ForwardViewportStage::initSkybox(const InitDesc& desc)
{
    auto dsls          = IDescriptorSetLayout::create(_render, {
                                                                   DescriptorSetLayoutDesc{
                                                                       .label    = "FwdSkybox_PerFrame_DSL",
                                                                       .set      = 0,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
                                                                   },
                                                                   DescriptorSetLayoutDesc{
                                                                       .label    = "FwdSkybox_Resource_DSL",
                                                                       .set      = 1,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                   },
                                                               });
    _skyboxFrameDSL    = dsls[0];
    _skyboxResourceDSL = dsls[1];

    _skyboxPPL = IPipelineLayout::create(_render, "FwdSkybox_PPL", {}, dsls);

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _skyboxPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Skybox.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _skyboxPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skyboxPipeline && _skyboxPipeline->recreate(ci), "Failed to create Forward Skybox pipeline");

    // Per-flight UBO + DS
    _skyboxDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                      .label     = "FwdSkybox_DSP",
                                                      .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                      .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
                                                  });

    SkyboxFrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skyboxFrameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                          .label       = std::format("FwdSkybox_Frame_UBO_{}", i),
                                                          .usage       = EBufferUsage::UniformBuffer,
                                                          .size        = sizeof(SkyboxFrameUBO),
                                                          .memoryUsage = EMemoryUsage::CpuToGpu,
                                                      });
        _skyboxFrameUBO[i]->writeData(&initialData, sizeof(SkyboxFrameUBO), 0);

        _skyboxFrameDS[i] = _skyboxDSP->allocateDescriptorSets(_skyboxFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_skyboxFrameDS[i], 0, _skyboxFrameUBO[i].get()),
        });
    }
}

// ── Debug ───────────────────────────────────────────────────────────
void ForwardViewportStage::initDebug(const InitDesc& desc)
{
    _debugDSL = IDescriptorSetLayout::create(_render,
                                             DescriptorSetLayoutDesc{
                                                 .label    = "FwdDebug_DSL",
                                                 .set      = 0,
                                                 .bindings = {
                                                     {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                                                 },
                                             });

    _debugPPL = IPipelineLayout::create(
        _render, "FwdDebug_PPL", {PushConstantRange{.offset = 0, .size = sizeof(DebugModelPC), .stageFlags = EShaderStage::Vertex}}, {_debugDSL});

    _debugPipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _debugPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/DebugRender.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::One,
                                   .dstColorBlendFactor = EBlendFactor::Zero,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _debugPipeline = IGraphicsPipeline::create(_render);
    _debugPipeline->recreate(_debugPipelineCI);

    _debugDSP       = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                           .maxSets   = 1,
                                                           .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1}},
                                                       });
    _debugUboDS     = _debugDSP->allocateDescriptorSets(_debugDSL);
    _debugUboBuffer = IBuffer::create(_render, BufferCreateInfo{
                                                   .label       = "FwdDebug_UBO",
                                                   .usage       = EBufferUsage::UniformBuffer,
                                                   .size        = sizeof(DebugUBO),
                                                   .memoryUsage = EMemoryUsage::CpuToGpu,
                                               });
    _render->getDescriptorHelper()->updateDescriptorSets({
                                                             IDescriptorSetHelper::genSingleBufferWrite(_debugUboDS, 0, EPipelineDescriptorType::UniformBuffer, _debugUboBuffer.get()),
                                                         },
                                                         {});
}

// ═══════════════════════════════════════════════════════════════════════
// Destroy
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::destroy()
{
    _litPasses.destroy();
    _unlitPass.destroy();
    for (auto& u : _skinningSSBO) u.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;

    // Simple
    _simplePipeline.reset();
    _simplePPL.reset();

    // Skybox
    _skyboxPipeline.reset();
    _skyboxPPL.reset();
    _skyboxFrameDSL.reset();
    _skyboxResourceDSL.reset();
    _skyboxDSP.reset();
    for (auto& u : _skyboxFrameUBO) u.reset();

    // Debug
    _debugPipeline.reset();
    _debugPPL.reset();
    _debugDSL.reset();
    _debugDSP.reset();
    _debugUboBuffer.reset();
    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};
    _getActiveScene = {};
    _getResourceResolveSystem = {};
    _getSceneSkyboxDescriptorSet = {};
    _getSceneEnvironmentLightingDescriptorSet = {};

    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::prepare(const RenderStageContext& ctx)
{
    _litPasses.beginFrame();
    _unlitPass.beginFrame();
    if (_simplePipeline) {
        _simplePipeline->beginFrame();
    }
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_debugPipeline) {
        _debugPipeline->beginFrame();
    }

    if (!ctx.frameData) return;

    updateSkinningBuffer(ctx);
    _litPasses.prepare(ctx);
    _unlitPass.prepare(ctx);

    // Skybox: update per-flight UBO
    SkyboxFrameUBO skyboxUBO{
        .projection = ctx.frameData->projection,
        .view       = FMath::dropTranslation(ctx.frameData->view),
    };
    _skyboxFrameUBO[ctx.flightIndex]->writeData(&skyboxUBO, sizeof(SkyboxFrameUBO), 0);
}

// ═══════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    executePasses(buildPassContext(ctx));
}

ForwardViewportStage::PassContext ForwardViewportStage::buildPassContext(const RenderStageContext& ctx)
{
    auto* activeScene           = _getActiveScene ? _getActiveScene() : nullptr;
    auto* resourceResolveSystem = _getResourceResolveSystem ? _getResourceResolveSystem() : nullptr;
    PassContext::SkyboxInput skybox{};
    PassContext::DebugDrawInput debugDraw{};

    if (activeScene && resourceResolveSystem && _getSceneSkyboxDescriptorSet) {
        const auto* skyboxState = resourceResolveSystem->findFirstSceneSkyboxState(activeScene);
        if (skyboxState && skyboxState->hasRenderableCubemap()) {
            skybox.descriptorSet = _getSceneSkyboxDescriptorSet(activeScene);
            skybox.mesh          = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
            for (const auto& [entity, sc, mc] : activeScene->getRegistry().view<SkyboxComponent, StaticMeshComponent>().each()) {
                if (mc.isResolved() && mc.getMesh()) {
                    skybox.mesh = mc.getMesh();
                }
                break;
            }
            skybox.bAvailable = skybox.descriptorSet && skybox.mesh;
        }
    }

    if (ctx.frameData) {
        auto appendDebugBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
        {
            if (items.empty()) {
                return;
            }
            YA_CORE_ASSERT(debugDraw.count < debugDraw.buckets.size(), "Forward debug bucket inventory overflow");
            debugDraw.buckets[debugDraw.count++] = PassContext::DebugDrawInput::Bucket{
                .items     = &items,
                .bSkinned  = bSkinned,
            };
            debugDraw.bHasDraws = true;
        };

        appendDebugBucket(ctx.frameData->drawBuckets.staticMeshes.pbrDrawItems, false);
        appendDebugBucket(ctx.frameData->drawBuckets.staticMeshes.phongDrawItems, false);
        appendDebugBucket(ctx.frameData->drawBuckets.staticMeshes.unlitDrawItems, false);
        appendDebugBucket(ctx.frameData->drawBuckets.staticMeshes.simpleDrawItems, false);
        appendDebugBucket(ctx.frameData->drawBuckets.staticMeshes.fallbackDrawItems, false);
        appendDebugBucket(ctx.frameData->drawBuckets.skinnedMeshes.pbrDrawItems, true);
        appendDebugBucket(ctx.frameData->drawBuckets.skinnedMeshes.phongDrawItems, true);
        appendDebugBucket(ctx.frameData->drawBuckets.skinnedMeshes.unlitDrawItems, true);
        appendDebugBucket(ctx.frameData->drawBuckets.skinnedMeshes.simpleDrawItems, true);
        appendDebugBucket(ctx.frameData->drawBuckets.skinnedMeshes.fallbackDrawItems, true);
    }

    return PassContext{
        .stageCtx = ctx,
        .activeScene = activeScene,
        .resourceResolveSystem = resourceResolveSystem,
        .sceneEnvironmentLightingDescriptorSet = (_getSceneEnvironmentLightingDescriptorSet && activeScene)
            ? _getSceneEnvironmentLightingDescriptorSet(activeScene)
            : DescriptorSetHandle{},
        .skybox = skybox,
        .debugDraw = debugDraw,
    };
}

void ForwardViewportStage::executePasses(const PassContext& passCtx)
{
    static constexpr std::array<EPass, 7> PASS_ORDER = {
        EPass::Skybox,
        EPass::PBR,
        EPass::Phong,
        EPass::Unlit,
        EPass::Simple,
        EPass::DirectionOverlay,
        EPass::Debug,
    };

    for (EPass pass : PASS_ORDER) {
        executePass(pass, passCtx);
    }
}

void ForwardViewportStage::executePass(EPass pass, const PassContext& passCtx)
{
    switch (pass) {
        case EPass::Skybox:
            drawSkybox(passCtx);
            break;
        case EPass::PBR:
            _litPasses.drawPBR(ForwardViewportLitPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .environmentLightingDescriptorSet = passCtx.sceneEnvironmentLightingDescriptorSet,
                .depthBufferShadowDS = _depthBufferShadowDS,
                .skinningDS = _skinningDS[passCtx.stageCtx.flightIndex],
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::Phong:
            _litPasses.drawPhong(ForwardViewportLitPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .skyboxDescriptorSet = passCtx.skybox.descriptorSet,
                .depthBufferShadowDS = _depthBufferShadowDS,
                .skinningDS = _skinningDS[passCtx.stageCtx.flightIndex],
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::Unlit:
            _unlitPass.draw(ForwardViewportUnlitPass::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .skinningDS = _skinningDS[passCtx.stageCtx.flightIndex],
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::Simple:
            drawSimple(passCtx);
            break;
        case EPass::DirectionOverlay:
            drawDirectionOverlay(passCtx);
            break;
        case EPass::Debug:
            drawDebug(passCtx);
            break;
    }
}

// ── Skybox draw ─────────────────────────────────────────────────────

void ForwardViewportStage::drawSkybox(const PassContext& passCtx)
{
    const auto& ctx = passCtx.stageCtx;
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;
    if (!passCtx.skybox.bAvailable) return;

    cmdBuf->debugBeginLabel("ForwardSkybox");

    cmdBuf->bindPipeline(_skyboxPipeline.get());
    setViewportAndScissor(cmdBuf, vpW, vpH);

    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {_skyboxFrameDS[ctx.flightIndex], passCtx.skybox.descriptorSet});
    passCtx.skybox.mesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

// ── Simple draw ─────────────────────────────────────────────────────

void ForwardViewportStage::drawSimple(const PassContext& passCtx)
{
    const auto& ctx = passCtx.stageCtx;
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.simpleDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.simpleDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;

    bool hasSimple = !staticItems.empty() || !skinnedItems.empty();
    if (!hasSimple) return;

    cmdBuf->debugBeginLabel("ForwardSimple");
    cmdBuf->bindPipeline(_simplePipeline.get());
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    SimplePC pc{};
    pc.view       = fd.view;
    pc.projection = fd.projection;

    // Simple material draw items (from snapshot)
    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* mat    = static_cast<SimpleMaterial*>(item.material);
            pc.model     = item.worldMatrix;
            pc.colorType = mat->colorType;
            cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(staticItems, false);
    drawBucket(skinnedItems, true);

    cmdBuf->debugEndLabel();
}

void ForwardViewportStage::drawDirectionOverlay(const PassContext& passCtx)
{
    const auto& ctx = passCtx.stageCtx;
    auto*       scene = passCtx.activeScene;
    if (!scene) return;

    const auto& dirView = scene->getRegistry().view<TransformComponent, DirectionComponent>();
    if (dirView.begin() == dirView.end()) return;

    auto* cmdBuf = ctx.cmdBuf;
    cmdBuf->debugBeginLabel("ForwardDirectionOverlay");
    cmdBuf->bindPipeline(_simplePipeline.get());
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    SimplePC pc{};
    pc.view       = ctx.frameData->view;
    pc.projection = ctx.frameData->projection;

    // Direction component visualization (from registry)
    auto* cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto* cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.3f, 1.0f, 0.3f));
    glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.1f, 1.0f, 0.1f));

    pc.colorType = _simpleDefaultColorType;
    for (auto entity : dirView) {
        const auto& [tc, dc] = dirView.get(entity);

        glm::mat4 worldTransform = glm::translate(glm::mat4(1.0), tc.getWorldPosition()) *
                                   glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));

        pc.model = glm::translate(glm::mat4(1.0), -tc.getForward()) * coneLocalTransf * worldTransform;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cone->draw(cmdBuf);

        pc.model = worldTransform * cylinderLocalTransf;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cylinder->draw(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

// ── Debug draw ──────────────────────────────────────────────────────

void ForwardViewportStage::drawDebug(const PassContext& passCtx)
{
    const auto& ctx = passCtx.stageCtx;
    if (_debugMode == DebugNone) return;

    auto*       cmdBuf = ctx.cmdBuf;
    const auto& fd     = *ctx.frameData;

    if (!passCtx.debugDraw.bHasDraws) return;

    auto vpW = ctx.viewportExtent.width;
    auto vpH = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Update debug UBO
    _debugUBO.projection = fd.projection;
    _debugUBO.view       = fd.view;
    _debugUBO.resolution = glm::ivec2(static_cast<int>(vpW), static_cast<int>(vpH));
    _debugUBO.time       = _getElapsedTimeSeconds ? static_cast<float>(_getElapsedTimeSeconds()) : 0.0f;
    _debugUboBuffer->writeData(&_debugUBO, sizeof(DebugUBO), 0);

    cmdBuf->debugBeginLabel("ForwardDebug");
    cmdBuf->bindPipeline(_debugPipeline.get());
    setViewportAndScissor(cmdBuf, vpW, vpH);

    // Draw all items from all buckets
    auto drawItems = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;
            DebugModelPC pc{.modelMat = item.worldMatrix};
            cmdBuf->bindDescriptorSets(_debugPPL.get(), 0, {_debugUboDS});
            cmdBuf->pushConstants(_debugPPL.get(), EShaderStage::Vertex, 0, sizeof(DebugModelPC), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };
    auto drawBucket = [&](const PassContext::DebugDrawInput::Bucket& bucket)
    {
        if (!bucket.items) {
            return;
        }
        drawItems(*bucket.items, bucket.bSkinned);
    };
    for (uint32_t bucketIndex = 0; bucketIndex < passCtx.debugDraw.count; ++bucketIndex) {
        drawBucket(passCtx.debugDraw.buckets[bucketIndex]);
    }

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::renderGUI()
{
    if (!ImGui::TreeNode("Viewport Renderer")) return;

    if (ImGui::TreeNode("Settings")) {
        ImGui::Combo("Simple Color Type", &_simpleDefaultColorType, "Normal\0UV\0Fixed");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debug")) {
        if (ImGui::TreeNode("Phong Debug")) {
            auto& phongDebug = _litPasses.phongDebug();
            bool bDebugNormal = (phongDebug.bDebugNormal != 0);
            bool bDebugDepth  = (phongDebug.bDebugDepth != 0);
            bool bDebugUV     = (phongDebug.bDebugUV != 0);

            if (ImGui::Checkbox("Debug Normal", &bDebugNormal)) phongDebug.bDebugNormal = bDebugNormal ? 1u : 0u;
            if (ImGui::Checkbox("Debug Depth", &bDebugDepth)) phongDebug.bDebugDepth = bDebugDepth ? 1u : 0u;
            if (ImGui::Checkbox("Debug UV", &bDebugUV)) phongDebug.bDebugUV = bDebugUV ? 1u : 0u;
            ImGui::DragFloat4("Float Param", glm::value_ptr(phongDebug.floatParam), 0.1f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Debug Render")) {
            const char* modeNames[] = {"None", "NormalColor", "NormalDir", "Depth", "UV"};
            int         mode        = static_cast<int>(_debugMode);
            if (ImGui::Combo("Mode", &mode, modeNames, IM_ARRAYSIZE(modeNames))) {
                EDebugMode newMode = static_cast<EDebugMode>(mode);
                if (newMode != _debugMode) {
                    if (newMode == DebugNormalDir) {
                        _debugPipelineCI.shaderDesc.defines = {"DEBUG_NORMAL_DIR"};
                        _debugPipeline->updateDesc(_debugPipelineCI);
                    }
                    else if (_debugMode == DebugNormalDir) {
                        _debugPipelineCI.shaderDesc.defines = {};
                        _debugPipeline->updateDesc(_debugPipelineCI);
                    }
                    _debugMode     = newMode;
                    _debugUBO.mode = static_cast<int>(_debugMode);
                }
            }
            ImGui::DragFloat4("Float Param", glm::value_ptr(_debugUBO.floatParam), 0.1f);
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Pipelines")) {
        if (ImGui::TreeNode("Simple")) {
            _simplePipeline->renderGUI();
            ImGui::TreePop();
        }
        _unlitPass.renderGUIPipelines();
        _litPasses.renderGUIPipelines();
        if (ImGui::TreeNode("Skybox")) {
            _skyboxPipeline->renderGUI();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Debug Pipeline")) {
            _debugPipeline->renderGUI();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    ImGui::TreePop();
}

// ═══════════════════════════════════════════════════════════════════════
// Shadow mapping toggle
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::applyShadowState(const ShadowRuntimeState& shadowState)
{
    _shadowState = shadowState;
    _litPasses.applyShadowState(shadowState);
}

// ═══════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(h);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(h);
        viewportHeight = -static_cast<float>(h);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(w), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, w, h);
}

} // namespace ya
