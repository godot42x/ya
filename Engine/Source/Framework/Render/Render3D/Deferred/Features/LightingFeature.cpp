#include "Render3D/Deferred/Features/LightingFeature.h"

#include "RHI/Render.h"

#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/Deferred/LightStage.h"

namespace ya
{

void LightingFeature::init(const FeatureInitContext& ctx)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<LightStage>();
    }
    _stage->setup(LightStage::SharedInputs{.frameAndLightDSL = ctx.frameAndLightDSL});
    _stage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = ctx.environmentLightingDSL,
    });
    _stage->init(ctx.render);
    _stage->setIBLSettings(bPBRDiffuseIBL, bPBRSpecularIBL);
}

void LightingFeature::shutdown()
{
    if (_stage) {
        _stage->destroy();
        _stage.reset();
    }
}

void LightingFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

void LightingFeature::applyShadowState(const ShadowRuntimeState& shadowState)
{
    if (_stage) {
        _stage->applyShadowState(shadowState);
    }
}

void LightingFeature::setIBLSettings(bool bEnablePBRDiffuseIBL, bool bEnablePBRSpecularIBL)
{
    bPBRDiffuseIBL  = bEnablePBRDiffuseIBL;
    bPBRSpecularIBL = bEnablePBRSpecularIBL;
    if (_stage) {
        _stage->setIBLSettings(bEnablePBRDiffuseIBL, bEnablePBRSpecularIBL);
    }
}

void LightingFeature::refreshDeviceFormats(const DeferredAttachmentFormats& formats)
{
    if (_stage) {
        _stage->refreshPipelineFormats(formats);
    }
}

LightingFeature::Resolved LightingFeature::resolve(const Settings& settings,
                                                   const Facts&    facts,
                                                   const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    if (!resolved.bEnabled) {
        return resolved;
    }

    resolved.target = ViewTargetRequirement{
        .role   = EViewAttachment::SceneColor,
        .format = settings.sceneColorFormat,
        .usage  = settings.sceneColorUsage,
    };
    return resolved;
}

Result<LightingFeature::Prepared, FeatureError> LightingFeature::prepareBindings(const Resolved&       resolved,
                                                                                  const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    prepared.frameAndLightDS      = ctx.frameAndLightDS;
    prepared.environmentLightingDS = ctx.environmentLightingDS;
    prepared.gBufferTextureDS     = allocateCombinedImageSamplerSet(*ctx.submission, ctx.stage->getGBufferTextureDSL(), 5);
    prepared.shadowDS             = allocateCombinedImageSamplerSet(
        *ctx.submission, ctx.stage->getShadowDSL(), 1u + static_cast<uint32_t>(MAX_POINT_LIGHTS));
    if (!prepared.frameAndLightDS || !prepared.gBufferTextureDS || !prepared.shadowDS) {
        return FeatureError::AllocationFailed;
    }
    ctx.stage->writeShadowDescriptors(prepared.shadowDS);

    return prepared;
}

Result<LightingFeature::GraphOutputs, FeatureError> LightingFeature::append(RenderGraph&               graph,
                                                                              Stage*                     stage,
                                                                              const FeatureGraphContext& ctx,
                                                                              const Facts&               facts,
                                                                              const Resolved&            resolved,
                                                                              const Prepared&            prepared,
                                                                              const Inputs&              inputs) const
{
    GraphOutputs outputs;
    if (!resolved.bEnabled || stage == nullptr || ctx.viewLease == nullptr || ctx.stageCtx == nullptr) {
        return outputs;
    }

    const std::shared_ptr<RenderTexture> sceneColorTarget = ctx.viewLease->find(resolved.target.role);
    if (sceneColorTarget == nullptr) {
        YA_CORE_ERROR("Lighting SceneColor target missing for View {}", ctx.viewId);
        return outputs;
    }
    outputs.sceneColor = graph.importTexture(makeImportedTextureDesc(
        *sceneColorTarget, "DeferredView.Color", EImageLayout::ShaderReadOnlyOptimal, resolved.target.usage));

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};

    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kLight, ctx.viewId),
        [inputs, renderArea, sceneColor = outputs.sceneColor](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(inputs.frame, inputs.frameRange);
            passBuilder.uniformRead(inputs.light, inputs.lightRange);
            passBuilder.read(inputs.albedo);
            passBuilder.read(inputs.normal);
            passBuilder.read(inputs.orm);
            passBuilder.read(inputs.shading);
            passBuilder.read(inputs.depth);
            if (inputs.ssao.has_value()) {
                passBuilder.read(*inputs.ssao);
            }
            if (inputs.shadowDepth.has_value()) {
                passBuilder.read(*inputs.shadowDepth);
            }
            if (inputs.environmentCubemap.has_value()) {
                passBuilder.read(*inputs.environmentCubemap);
            }
            if (inputs.environmentIrradiance.has_value()) {
                passBuilder.read(*inputs.environmentIrradiance);
            }
            if (inputs.environmentPrefilter.has_value()) {
                passBuilder.read(*inputs.environmentPrefilter);
            }
            if (inputs.environmentBrdfLut.has_value()) {
                passBuilder.read(*inputs.environmentBrdfLut);
            }
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = sceneColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stage, stageCtx = *ctx.stageCtx, prepared, inputs](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            stage->writeGBufferTextureDescriptors(
                prepared.gBufferTextureDS,
                rgCtx.getBindingContext(),
                inputs.albedo,
                inputs.normal,
                inputs.orm,
                inputs.shading,
                inputs.depth,
                inputs.ssao);

            rgCtx.beginDeclaredRasterRendering();
            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            stage->execute(
                stageCtx,
                prepared.frameAndLightDS,
                prepared.environmentLightingDS,
                prepared.gBufferTextureDS,
                prepared.shadowDS);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
