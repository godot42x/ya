#include "Render3D/Deferred/DeferredPipelineRecipe.h"

#include "Core/Log.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewTargetStore.h"

namespace ya
{

namespace
{

constexpr size_t kGBufferColorCount = 4;

} // namespace

Result<std::vector<DeferredPipelineRecipe::ResolvedView>, FeatureError>
DeferredPipelineRecipe::resolve(const Config& config, const RenderFramePlan& plan)
{
    if (config.gBufferFormats.colorFormats.size() < kGBufferColorCount ||
        !config.gBufferFormats.depthFormat.has_value()) {
        YA_CORE_ERROR("Deferred composition requires {} GBuffer color formats and a depth format",
                      kGBufferColorCount);
        return FeatureError::InvalidInput;
    }

    GBufferFeature::Settings gbufferSettings;
    gbufferSettings.bEnabled      = true;
    gbufferSettings.albedoFormat  = config.gBufferFormats.colorFormats[0];
    gbufferSettings.normalFormat  = config.gBufferFormats.colorFormats[1];
    gbufferSettings.ormFormat     = config.gBufferFormats.colorFormats[2];
    gbufferSettings.shadingFormat = config.gBufferFormats.colorFormats[3];
    gbufferSettings.depthFormat   = *config.gBufferFormats.depthFormat;

    LightingFeature::Settings lightingSettings;
    lightingSettings.bEnabled         = true;
    lightingSettings.sceneColorFormat = config.viewColorFormat;
    lightingSettings.sceneColorUsage  = config.viewColorUsage;

    const GBufferFeature  gbufferFeature;
    const SSAOFeature     ssaoFeature;

    std::vector<ResolvedView> resolved;
    resolved.reserve(plan.sceneRender.views().size());
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        const SceneViewTask* task = recording.task;
        if (task == nullptr || recording.frameData == nullptr || task->desc.viewId == 0 || !task->output.hasExtent()) {
            continue;
        }

        ResolvedView view;
        view.viewId     = task->desc.viewId;
        view.viewExtent = task->output.extent;

        view.gbuffer = gbufferFeature.resolve(
            gbufferSettings,
            GBufferFeature::Facts{.viewId = view.viewId, .viewExtent = view.viewExtent},
            GBufferFeature::Inputs{});

        view.ssao = ssaoFeature.resolve(
            config.ssao,
            SSAOFeature::Facts{
                .viewExtent = view.viewExtent,
                .frameData  = recording.frameData,
                .bReverseY  = config.bReverseViewportY,
            },
            SSAOFeature::Inputs{});

        view.lighting = LightingFeature::resolve(
            lightingSettings,
            LightingFeature::Facts{.viewExtent = view.viewExtent},
            LightingFeature::Inputs{});

        resolved.push_back(view);
    }
    return resolved;
}

Result<DeferredPipelineRecipe::PreparedView, FeatureError>
DeferredPipelineRecipe::prepareBindings(const ResolvedView&   resolved,
                                        const Stages&         stages,
                                        const PrepareContext& ctx)
{
    if (ctx.submission == nullptr) {
        return FeatureError::MissingContext;
    }

    PreparedView prepared;
    prepared.viewId          = resolved.viewId;
    prepared.frameRange      = ctx.frameRange;
    prepared.lightRange      = ctx.lightRange;
    prepared.ssaoFrameRange  = ctx.ssaoFrameRange;

    const GBufferFeature gbufferFeature;
    prepared.gbuffer = gbufferFeature.prepareBindings(
        resolved.gbuffer,
        GBufferFeature::PrepareInputs{
            .frameAndLightDS = ctx.frameAndLightDS,
            .skinningDS      = ctx.skinningDS,
        });

    const SSAOFeature ssaoFeature;
    auto ssaoPrepared = ssaoFeature.prepareBindings(
        resolved.ssao,
        SSAOFeature::PrepareContext{
            .submission = ctx.submission,
            .stage      = stages.ssao,
            .frameDS    = ctx.ssaoFrameDS.value_or(DescriptorSetHandle{}),
        });
    if (ssaoPrepared.isErr()) {
        return ssaoPrepared.error();
    }
    prepared.ssao = ssaoPrepared.value();

    auto lightingPrepared = LightingFeature::prepareBindings(
        resolved.lighting,
        LightingFeature::PrepareContext{
            .submission            = ctx.submission,
            .stage                 = stages.light,
            .frameAndLightDS       = ctx.frameAndLightDS,
            .environmentLightingDS = ctx.environmentLightingDS,
        });
    if (lightingPrepared.isErr()) {
        return lightingPrepared.error();
    }
    prepared.lighting = lightingPrepared.value();

    return prepared;
}

Result<DeferredPipelineRecipe::GraphOutputs, FeatureError>
DeferredPipelineRecipe::appendGraph(const ResolvedView&  resolved,
                                    const PreparedView&  prepared,
                                    const AppendContext& ctx)
{
    if (ctx.stages == nullptr || ctx.viewLease == nullptr) {
        return FeatureError::MissingContext;
    }
    if (!resolved.gbuffer.bEnabled || !resolved.lighting.bEnabled) {
        return FeatureError::InvalidInput;
    }

    GraphOutputs outputs;

    FeatureGraphContext featureCtx;
    featureCtx.graph             = &ctx.graph;
    featureCtx.viewLease         = ctx.viewLease;
    featureCtx.stageCtx          = &ctx.stageCtx;
    featureCtx.viewId            = resolved.viewId;
    featureCtx.predecessor       = ctx.predecessor;
    featureCtx.bReverseViewportY = ctx.bReverseViewportY;

    const GBufferFeature gbufferFeature;
    outputs.gbuffer = gbufferFeature.append(
        ctx.graph,
        ctx.stages->gBuffer,
        featureCtx,
        GBufferFeature::Facts{.viewId = resolved.viewId, .viewExtent = resolved.viewExtent},
        resolved.gbuffer,
        prepared.gbuffer,
        GBufferFeature::Inputs{
            .frame      = ctx.frameBuffer,
            .frameRange = prepared.frameRange,
            .light      = ctx.lightBuffer,
            .lightRange = prepared.lightRange,
            .skinning   = ctx.skinningBuffer,
        });

    const SSAOFeature ssaoFeature;
    auto ssaoOutputs = ssaoFeature.append(
        ctx.graph,
        ctx.stages->ssao,
        featureCtx,
        SSAOFeature::Facts{.viewExtent = resolved.viewExtent},
        resolved.ssao,
        prepared.ssao,
        SSAOFeature::Inputs{
            .frame      = ctx.ssaoFrameBuffer.value_or(RGBufferHandle{}),
            .frameRange = prepared.ssaoFrameRange,
            .albedo     = outputs.gbuffer.albedo,
            .normal     = outputs.gbuffer.normal,
            .depth      = outputs.gbuffer.depth,
        });
    if (ssaoOutputs.isErr()) {
        YA_CORE_ERROR("SSAO append failed for View {}", resolved.viewId);
        return ssaoOutputs.error();
    }
    outputs.ssao = ssaoOutputs.value();

    LightingFeature::Inputs lightingInputs;
    lightingInputs.albedo      = outputs.gbuffer.albedo;
    lightingInputs.normal      = outputs.gbuffer.normal;
    lightingInputs.orm         = outputs.gbuffer.orm;
    lightingInputs.shading     = outputs.gbuffer.shading;
    lightingInputs.depth       = outputs.gbuffer.depth;
    lightingInputs.frame       = ctx.frameBuffer;
    lightingInputs.frameRange  = prepared.frameRange;
    lightingInputs.light       = ctx.lightBuffer;
    lightingInputs.lightRange  = prepared.lightRange;
    if (outputs.ssao.ao.isValid()) {
        lightingInputs.ssao = outputs.ssao.ao.get();
    }
    if (ctx.shadow.shadowDepth.has_value()) {
        lightingInputs.shadowDepth = *ctx.shadow.shadowDepth;
    }
    if (ctx.environmentLighting != nullptr && ctx.environmentLighting->isComplete()) {
        lightingInputs.environmentCubemap = ctx.graph.importTexture(makeImportedTextureDesc(
            ctx.environmentLighting->cubemap, "DeferredLight.Environment.Cubemap", EImageLayout::ShaderReadOnlyOptimal));
        lightingInputs.environmentIrradiance = ctx.graph.importTexture(makeImportedTextureDesc(
            ctx.environmentLighting->irradiance, "DeferredLight.Environment.Irradiance", EImageLayout::ShaderReadOnlyOptimal));
        lightingInputs.environmentPrefilter = ctx.graph.importTexture(makeImportedTextureDesc(
            ctx.environmentLighting->prefilter, "DeferredLight.Environment.Prefilter", EImageLayout::ShaderReadOnlyOptimal));
        lightingInputs.environmentBrdfLut = ctx.graph.importTexture(makeImportedTextureDesc(
            *ctx.environmentLighting->brdfLut, "DeferredLight.Environment.BrdfLut", EImageLayout::ShaderReadOnlyOptimal));
    }

    outputs.lighting = LightingFeature::append(
        ctx.graph,
        ctx.stages->light,
        featureCtx,
        LightingFeature::Facts{.viewExtent = resolved.viewExtent},
        resolved.lighting,
        prepared.lighting,
        lightingInputs);

    if (!outputs.lighting.sceneColor.isValid()) {
        YA_CORE_ERROR("Lighting append produced no SceneColor for View {}", resolved.viewId);
        return FeatureError::MissingDependency;
    }
    return outputs;
}

} // namespace ya
