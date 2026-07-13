#include "ForwardViewportStage.h"

#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Scene/Scene.h"

#include "imgui.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Common vertex attributes
// ═══════════════════════════════════════════════════════════════════════

static const std::vector<VertexAttribute> kSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

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
        _skinningSSBO[i] = _render->getResourceFactory()->createBuffer(
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
    _auxPasses.init(ForwardViewportAuxPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .getElapsedTimeSeconds = desc.getElapsedTimeSeconds,
    });
}

void ForwardViewportStage::refreshPipelineFormats(const RenderAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }

    _litPasses.refreshPipelineFormats(formats);
    _unlitPass.refreshPipelineFormats(formats);
    _auxPasses.refreshPipelineFormats(formats);
}

void ForwardViewportStage::setDepthBufferShadowDescriptorSet(DescriptorSetHandle depthBufferShadowDS)
{
    _depthBufferShadowDS = depthBufferShadowDS;
}

// ═══════════════════════════════════════════════════════════════════════
// Destroy
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::destroy()
{
    _litPasses.destroy();
    _unlitPass.destroy();
    _auxPasses.destroy();
    for (auto& u : _skinningSSBO) u.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;
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
    _auxPasses.beginFrame();

    if (!ctx.frameData) return;

    updateSkinningBuffer(ctx);
    _litPasses.prepare(ctx);
    _unlitPass.prepare(ctx);
    _auxPasses.prepare(ctx);
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
            _auxPasses.drawSkybox(ForwardViewportAuxPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .activeScene = passCtx.activeScene,
                .skybox = {
                    .bAvailable = passCtx.skybox.bAvailable,
                    .descriptorSet = passCtx.skybox.descriptorSet,
                    .mesh = passCtx.skybox.mesh,
                },
                .debugDraw = {},
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
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
            _auxPasses.drawSimple(ForwardViewportAuxPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .activeScene = passCtx.activeScene,
                .skybox = {},
                .debugDraw = {},
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::DirectionOverlay:
            _auxPasses.drawDirectionOverlay(ForwardViewportAuxPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .activeScene = passCtx.activeScene,
                .skybox = {},
                .debugDraw = {},
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::Debug:
            _auxPasses.drawDebug(ForwardViewportAuxPasses::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .activeScene = passCtx.activeScene,
                .skybox = {},
                .debugDraw = [&]() {
                    ForwardViewportAuxPasses::DrawContext::DebugDrawInput debugDraw{};
                    debugDraw.count = passCtx.debugDraw.count;
                    debugDraw.bHasDraws = passCtx.debugDraw.bHasDraws;
                    for (uint32_t i = 0; i < passCtx.debugDraw.count; ++i) {
                        debugDraw.buckets[i] = {
                            .items = passCtx.debugDraw.buckets[i].items,
                            .bSkinned = passCtx.debugDraw.buckets[i].bSkinned,
                        };
                    }
                    return debugDraw;
                }(),
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::renderGUI()
{
    if (!ImGui::TreeNode("Viewport Renderer")) return;

    if (ImGui::TreeNode("Settings")) {
        _auxPasses.renderSettingsGUI();
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
            _auxPasses.renderDebugGUI();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Pipelines")) {
        _unlitPass.renderGUIPipelines();
        _litPasses.renderGUIPipelines();
        _auxPasses.renderGUIPipelines();
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
