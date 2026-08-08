#include "ForwardViewportStage.h"

#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Render.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Render3D/Scene.h"

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

    _litPasses.init(ForwardViewportLitPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .shadowState = desc.shadowState,
        .skinningDSL = _skinningDSL,
        .pbrFrameDSL = desc.pbrFrameDSL,
        .phongFrameDSL = desc.phongFrameDSL,
        .runtimeServices = _runtimeServices,
    });
    _unlitPass.init(ForwardViewportUnlitPass::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .skinningDSL = _skinningDSL,
        .unlitFrameDSL = desc.unlitFrameDSL,
        .runtimeServices = _runtimeServices,
    });
    _auxPasses.init(ForwardViewportAuxPasses::InitDesc{
        .render = desc.render,
        .renderPass = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .skyboxFrameDSL = desc.skyboxFrameDSL,
        .runtimeServices = _runtimeServices,
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
    return PassContext{
        .stageCtx = ctx,
        .activeScene = activeScene,
        .resourceResolveSystem = resourceResolveSystem,
        .sceneEnvironmentLightingDescriptorSet = (_runtimeServices && activeScene)
            ? _runtimeServices->getSceneEnvironmentLightingDescriptorSet(activeScene)
            : DescriptorSetHandle{},
        .skybox = buildSkyboxInput(activeScene, resourceResolveSystem),
        .debugDraw = buildDebugDrawInput(ctx.frameData),
    };
}

ForwardViewportStage::PassContext::SkyboxInput ForwardViewportStage::buildSkyboxInput(
    Scene* activeScene,
    ResourceResolveSystem* resourceResolveSystem) const
{
    PassContext::SkyboxInput input{};
    if (!activeScene || !_runtimeServices) {
        return input;
    }

    input.descriptorSet = _runtimeServices->getSceneSkyboxDescriptorSet(activeScene);
    if (!resourceResolveSystem) {
        return input;
    }

    const auto* skyboxState = resourceResolveSystem->findFirstSceneSkyboxState(activeScene);
    if (!skyboxState || !skyboxState->hasRenderableCubemap()) {
        return input;
    }

    input.mesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    for (const auto& [entity, skybox, mesh] : activeScene->getRegistry().view<SkyboxComponent, StaticMeshComponent>().each()) {
        (void)entity;
        (void)skybox;
        if (mesh.isResolved() && mesh.getMesh()) {
            input.mesh = mesh.getMesh();
        }
        break;
    }
    input.bAvailable = input.descriptorSet && input.mesh;
    return input;
}

ForwardViewportStage::PassContext::DebugDrawInput ForwardViewportStage::buildDebugDrawInput(
    const RenderFrameData* frameData) const
{
    PassContext::DebugDrawInput input{};
    if (!frameData) {
        return input;
    }

    const auto appendBucket = [&input](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        if (items.empty()) {
            return;
        }
        YA_CORE_ASSERT(input.count < input.buckets.size(), "Forward debug bucket inventory overflow");
        input.buckets[input.count++] = {
            .items    = &items,
            .bSkinned = bSkinned,
        };
        input.bHasDraws = true;
    };

    appendBucket(frameData->drawBuckets.staticMeshes.pbrDrawItems, false);
    appendBucket(frameData->drawBuckets.staticMeshes.phongDrawItems, false);
    appendBucket(frameData->drawBuckets.staticMeshes.unlitDrawItems, false);
    appendBucket(frameData->drawBuckets.staticMeshes.simpleDrawItems, false);
    appendBucket(frameData->drawBuckets.staticMeshes.fallbackDrawItems, false);
    appendBucket(frameData->drawBuckets.skinnedMeshes.pbrDrawItems, true);
    appendBucket(frameData->drawBuckets.skinnedMeshes.phongDrawItems, true);
    appendBucket(frameData->drawBuckets.skinnedMeshes.unlitDrawItems, true);
    appendBucket(frameData->drawBuckets.skinnedMeshes.simpleDrawItems, true);
    appendBucket(frameData->drawBuckets.skinnedMeshes.fallbackDrawItems, true);
    return input;
}

ForwardViewportAuxPasses::DrawContext ForwardViewportStage::makeAuxDrawContext(
    const PassContext& passCtx,
    bool               bIncludeSkybox,
    bool               bIncludeDebug,
    bool               bIncludeDirection) const
{
    return ForwardViewportAuxPasses::DrawContext{
        .stageCtx = passCtx.stageCtx,
        .activeScene = passCtx.activeScene,
        .skybox = bIncludeSkybox ? passCtx.skybox : ForwardViewportAuxPasses::DrawContext::SkyboxInput{},
        .debugDraw = bIncludeDebug ? passCtx.debugDraw : ForwardViewportAuxPasses::DrawContext::DebugDrawInput{},
        .directionGizmos = bIncludeDirection ? passCtx.directionGizmos : std::vector<ForwardDirectionGizmoInput>{},
        .skyboxFrameDescriptorSet = bIncludeSkybox ? passCtx.skyboxFrameDescriptorSet : nullptr,
        .bReverseViewportY = bReverseViewportY,
    };
}

ForwardViewportLitPasses::DrawContext ForwardViewportStage::makePBRDrawContext(const PassContext& passCtx) const
{
    return ForwardViewportLitPasses::DrawContext{
        .stageCtx = passCtx.stageCtx,
        .environmentLightingDescriptorSet = passCtx.sceneEnvironmentLightingDescriptorSet,
        .depthBufferShadowDS = _depthBufferShadowDS,
        .skinningDS = passCtx.skinningDescriptorSet,
        .pbrFrameDescriptorSet = passCtx.pbrFrameDescriptorSet,
        .bReverseViewportY = bReverseViewportY,
    };
}

ForwardViewportLitPasses::DrawContext ForwardViewportStage::makePhongDrawContext(const PassContext& passCtx) const
{
    return ForwardViewportLitPasses::DrawContext{
        .stageCtx = passCtx.stageCtx,
        .skyboxDescriptorSet = passCtx.skybox.descriptorSet,
        .depthBufferShadowDS = _depthBufferShadowDS,
        .skinningDS = passCtx.skinningDescriptorSet,
        .phongFrameDescriptorSet = passCtx.phongFrameDescriptorSet,
        .bReverseViewportY = bReverseViewportY,
    };
}

ForwardViewportUnlitPass::DrawContext ForwardViewportStage::makeUnlitDrawContext(const PassContext& passCtx) const
{
    return ForwardViewportUnlitPass::DrawContext{
        .stageCtx = passCtx.stageCtx,
        .skinningDS = passCtx.skinningDescriptorSet,
        .unlitFrameDescriptorSet = passCtx.unlitFrameDescriptorSet,
        .bReverseViewportY = bReverseViewportY,
    };
}

void ForwardViewportStage::executePass(EPass pass, const PassContext& passCtx)
{
    switch (pass) {
        case EPass::Skybox:
            _auxPasses.drawSkybox(makeAuxDrawContext(passCtx, true));
            break;
        case EPass::PBR:
            _litPasses.drawPBR(makePBRDrawContext(passCtx));
            break;
        case EPass::Phong:
            _litPasses.drawPhong(makePhongDrawContext(passCtx));
            break;
        case EPass::Unlit:
            _unlitPass.draw(makeUnlitDrawContext(passCtx));
            break;
        case EPass::Simple:
            _auxPasses.drawSimple(makeAuxDrawContext(passCtx));
            break;
        case EPass::DirectionOverlay:
            _auxPasses.drawDirectionOverlay(makeAuxDrawContext(passCtx, false, false, true));
            break;
        case EPass::Debug:
            _auxPasses.drawDebug(makeAuxDrawContext(passCtx, false, true));
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Shadow mapping toggle
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::applyShadowState(const ShadowRuntimeState& shadowState)
{
    _shadowState = shadowState;
    _litPasses.applyShadowState(shadowState);
}

} // namespace ya
