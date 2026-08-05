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

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Common vertex attributes
// ═══════════════════════════════════════════════════════════════════════

static const std::vector<VertexAttribute> kSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

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
    _skinningDSL                              = desc.skinningDSL;
    _depthBufferShadowDS                      = desc.depthBufferShadowDS;
    _shadowState                              = desc.shadowState;
    _runtimeServices                          = desc.runtimeServices;
    _getFrameIndex                            = desc.getFrameIndex;
    _getElapsedTimeSeconds                    = desc.getElapsedTimeSeconds;

    _litPasses.init(ForwardViewportLitPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .shadowState = desc.shadowState,
        .skinningDSL = _skinningDSL,
        .pbrFrameDSL = desc.pbrFrameDSL,
        .phongFrameDSL = desc.phongFrameDSL,
        .getFrameIndex = desc.getFrameIndex,
        .getElapsedTimeSeconds = desc.getElapsedTimeSeconds,
    });
    _unlitPass.init(ForwardViewportUnlitPass::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .skinningDSL = _skinningDSL,
        .unlitFrameDSL = desc.unlitFrameDSL,
        .getFrameIndex = desc.getFrameIndex,
        .getElapsedTimeSeconds = desc.getElapsedTimeSeconds,
    });
    _auxPasses.init(ForwardViewportAuxPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .skyboxFrameDSL = desc.skyboxFrameDSL,
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
    _skinningDSL.reset();
    _runtimeServices = nullptr;
    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};

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

    _framePayloads = {};
    _litPasses.prepare(ctx, _framePayloads);
    _unlitPass.prepare(ctx, _framePayloads.unlitFrame);
    _auxPasses.prepare(ctx, _framePayloads.skyboxFrame);
}

// ═══════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::execute(const RenderStageContext& ctx)
{
    // Conformance stub: the top-level Forward graph calls the per-pass
    // overloads (FG-702) with an explicit current-flight binding. A bare
    // IRenderStage execute has no binding and must not hide the pass order.
    YA_CORE_WARN("ForwardViewportStage::execute(ctx) is a conformance stub; graph passes must use the per-pass overloads");
}

void ForwardViewportStage::executeSkybox(const RenderStageContext& ctx,
                                         const ForwardFrameResourceSet::Binding& binding,
                                         const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    passCtx.skyboxFrameDescriptorSet = binding.skyboxFrameDescriptorSet;
    executePass(EPass::Skybox, passCtx);
}

void ForwardViewportStage::executePBR(const RenderStageContext& ctx,
                                      const ForwardFrameResourceSet::Binding& binding,
                                      const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    passCtx.skinningDescriptorSet   = binding.skinningDescriptorSet;
    passCtx.pbrFrameDescriptorSet   = binding.pbrFrameDescriptorSet;
    executePass(EPass::PBR, passCtx);
}

void ForwardViewportStage::executePhong(const RenderStageContext& ctx,
                                        const ForwardFrameResourceSet::Binding& binding,
                                        const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    passCtx.skinningDescriptorSet    = binding.skinningDescriptorSet;
    passCtx.phongFrameDescriptorSet  = binding.phongFrameDescriptorSet;
    executePass(EPass::Phong, passCtx);
}

void ForwardViewportStage::executeUnlit(const RenderStageContext& ctx,
                                        const ForwardFrameResourceSet::Binding& binding,
                                        const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    passCtx.skinningDescriptorSet   = binding.skinningDescriptorSet;
    passCtx.unlitFrameDescriptorSet = binding.unlitFrameDescriptorSet;
    executePass(EPass::Unlit, passCtx);
}

void ForwardViewportStage::executeSimple(const RenderStageContext& ctx, const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    executePass(EPass::Simple, passCtx);
}

void ForwardViewportStage::executeDirection(const RenderStageContext& ctx,
                                            std::vector<ForwardDirectionGizmoInput> directionGizmos,
                                            const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    passCtx.directionGizmos = std::move(directionGizmos);
    executePass(EPass::DirectionOverlay, passCtx);
}

void ForwardViewportStage::executeDebug(const RenderStageContext& ctx, const PassContext* snapshot)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto passCtx = snapshot ? *snapshot : buildPassContext(ctx);
    executePass(EPass::Debug, passCtx);
}

ForwardViewportStage::PassContext ForwardViewportStage::buildPassContext(const RenderStageContext& ctx)
{
    auto* activeScene           = _runtimeServices ? _runtimeServices->getActiveScene() : nullptr;
    auto* resourceResolveSystem = _runtimeServices ? _runtimeServices->getResourceResolveSystem() : nullptr;
    PassContext::SkyboxInput skybox{};
    PassContext::DebugDrawInput debugDraw{};

    if (activeScene && _runtimeServices) {
        skybox.descriptorSet = _runtimeServices->getSceneSkyboxDescriptorSet(activeScene);
    }

    if (activeScene && resourceResolveSystem && _runtimeServices) {
        const auto* skyboxState = resourceResolveSystem->findFirstSceneSkyboxState(activeScene);
        if (skyboxState && skyboxState->hasRenderableCubemap()) {
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
        .sceneEnvironmentLightingDescriptorSet = (_runtimeServices && activeScene)
            ? _runtimeServices->getSceneEnvironmentLightingDescriptorSet(activeScene)
            : DescriptorSetHandle{},
        .skybox = skybox,
        .debugDraw = debugDraw,
    };
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
                .skyboxFrameDescriptorSet = passCtx.skyboxFrameDescriptorSet,
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
                .skinningDS = passCtx.skinningDescriptorSet,
                .pbrFrameDescriptorSet = passCtx.pbrFrameDescriptorSet,
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
                .skinningDS = passCtx.skinningDescriptorSet,
                .phongFrameDescriptorSet = passCtx.phongFrameDescriptorSet,
                .setViewportAndScissor = [this](ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
                {
                    setViewportAndScissor(cmdBuf, w, h);
                },
            });
            break;
        case EPass::Unlit:
            _unlitPass.draw(ForwardViewportUnlitPass::DrawContext{
                .stageCtx = passCtx.stageCtx,
                .skinningDS = passCtx.skinningDescriptorSet,
                .unlitFrameDescriptorSet = passCtx.unlitFrameDescriptorSet,
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
