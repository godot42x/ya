#include "Render3D/Deferred/DeferredPipelineRecipe.h"

#include "Core/Log.h"

#include <format>
#include <string>
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Composition/ColorContinuation.h"
#include "Render3D/Deferred/DeferredPassNames.h"

namespace ya
{

namespace
{

constexpr size_t kGBufferColorCount = 4;

[[nodiscard]] RGBufferRange toRange(const FrameUploadArena::Allocation& allocation)
{
    return RGBufferRange{.offset = allocation.offset, .size = allocation.size};
}

} // namespace

std::vector<ViewAttachmentDesc> DeferredPipelineRecipe::ResolvedView::collectViewTargets() const
{
    // Stable order: the fold runs in map order, so the request builder publishes
    // the same list every frame and the View target store only rebuilds when the
    // composition actually changes.
    std::vector<ViewAttachmentDesc> targets;
    targets.reserve(12);
    foldTargets(invocations, targets);
    return targets;
}

std::string DeferredPipelineRecipe::ResolvedView::describe() const
{
    std::string out = std::format("Deferred View {} ({}x{})\n", viewId, viewExtent.width, viewExtent.height);
    foldDescribe(invocations, out);

    out += std::format("  targets: {}", collectViewTargets().size());
    for (const ViewAttachmentDesc& target : collectViewTargets()) {
        out += std::format(" {}", static_cast<uint32_t>(target.role));
    }
    out += "\n";
    return out;
}

Result<std::vector<DeferredPipelineRecipe::ResolvedView>, FeatureError>
DeferredPipelineRecipe::resolve(const Config& config, const RenderFramePlan& plan)
{
    if (config.gBufferFormats.colorFormats.size() < kGBufferColorCount ||
        !config.gBufferFormats.depthFormat.has_value()) {
        YA_CORE_ERROR("Deferred composition requires {} GBuffer color formats and a depth format",
                      kGBufferColorCount);
        return FeatureError::InvalidInput;
    }

    const ShadowFeature shadowFeature;

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

    const SkyboxFeature::Settings        skyboxSettings{};
    const Sprite2DFeature::Settings      spriteSettings{};
    const OverlayFeature::Settings       overlaySettings{};

    // Shared features take this pipeline's graph vocabulary from the recipe
    // (the pipeline owns the map, so it owns the names on it).
    EntityIdFeature::Settings entityIdSettings;
    entityIdSettings.passName    = deferred_pass_names::kEntityId;
    entityIdSettings.importLabel = deferred_pass_names::kEntityIdImport;

    const GBufferFeature       gbufferFeature;
    const SSAOFeature          ssaoFeature;
    const LightingFeature      lightingFeature;
    const SkyboxFeature        skyboxFeature;
    const Sprite2DFeature      spriteFeature;
    const OverlayFeature       overlayFeature;
    const BloomFeature         bloomFeature;
    const EntityIdFeature      entityIdFeature;
    const FinalizeFeature      finalizeFeature;

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

        view.shadow() = shadowFeature.resolve(
            config.shadow,
            ShadowFeature::Facts{},
            ShadowFeature::Inputs{});

        view.gbuffer() = gbufferFeature.resolve(
            gbufferSettings,
            GBufferFeature::Facts{.viewId = view.viewId, .viewExtent = view.viewExtent},
            GBufferFeature::Inputs{});

        view.ssao() = ssaoFeature.resolve(
            config.ssao,
            SSAOFeature::Facts{
                .viewExtent = view.viewExtent,
                .frameData  = recording.frameData,
                .bReverseY  = config.bReverseViewportY,
            },
            SSAOFeature::Inputs{});

        view.lighting() = lightingFeature.resolve(
            lightingSettings,
            LightingFeature::Facts{.viewExtent = view.viewExtent},
            LightingFeature::Inputs{});

        view.skybox() = skyboxFeature.resolve(
            skyboxSettings,
            SkyboxFeature::Facts{.viewExtent = view.viewExtent, .frameData = recording.frameData},
            SkyboxFeature::Inputs{});

        view.sprites() = spriteFeature.resolve(
            spriteSettings,
            Sprite2DFeature::Facts{.viewExtent = view.viewExtent, .frameData = recording.frameData},
            Sprite2DFeature::Inputs{});

        BloomFeature::Settings bloomSettings = config.bloom;
        bloomSettings.importLabelPrefix      = deferred_pass_names::kBloomImportPrefix;

        view.bloom() = bloomFeature.resolve(
            bloomSettings,
            BloomFeature::Facts{.viewExtent = view.viewExtent},
            BloomFeature::Inputs{});

        view.overlay() = overlayFeature.resolve(
            overlaySettings,
            OverlayFeature::Facts{.viewExtent = view.viewExtent},
            OverlayFeature::Inputs{});

        view.entityId() = entityIdFeature.resolve(
            entityIdSettings,
            EntityIdFeature::Facts{.viewExtent = view.viewExtent},
            EntityIdFeature::Inputs{});

        view.finalize() = finalizeFeature.resolve(
            config.finalize,
            FinalizeFeature::Facts{.viewExtent = view.viewExtent},
            FinalizeFeature::Inputs{});

        // The extension at each point resolves from the same settings snapshot
        // as the core features: it declares its own requirements and builds its
        // own per-View values, exactly like a core feature, but it is reached
        // through the extension tuple rather than a named accessor.
        std::get<Invocation<SSRFeature>>(view.extensions).resolved = SSRFeature{}.resolve(
            config.ssr,
            SSRFeature::Facts{
                .viewExtent = view.viewExtent,
                .frameData  = recording.frameData,
                .bReverseY  = config.bReverseViewportY,
            },
            SSRFeature::Inputs{});

        resolved.push_back(view);
    }
    return resolved;
}

Result<DeferredPipelineRecipe::PreparedView, FeatureError>
DeferredPipelineRecipe::prepareBindings(const ResolvedView&   resolved,
                                        const Stages&         stages,
                                        const PrepareContext& ctx)
{
    if (ctx.submission == nullptr || ctx.frameBinding == nullptr) {
        return FeatureError::MissingContext;
    }

    // The prepare schedule: one row per feature, in map order. Each row states
    // the only thing this recipe knows for that feature -- which device object
    // and which shared binding its typed prepare context gets. `prepareFeature`
    // owns the call, the failure check and the log, so a row cannot forget any
    // of them. Features are constructed per row: they hold no prepare state
    // (device ownership lives on the pipeline's long-lived instances).
    PreparedView prepared;
    prepared.viewId     = resolved.viewId;
    prepared.frameRange = toRange(ctx.frameBinding->frame);
    prepared.lightRange = toRange(ctx.frameBinding->light);

    const ShadowFeature::PrepareContext shadowPrepare{
        .submission = ctx.submission,
        .stage      = stages.shadow,
        .view       = ctx.view,
    };
    if (auto step = prepareFeature(ShadowFeature{},
                                   resolved.shadow(),
                                   shadowPrepare,
                                   prepared.shadow());
        step.isErr())
        return step.error();

    const GBufferFeature::PrepareContext gbufferPrepare{
        .frameAndLightDS = ctx.frameBinding->frameAndLightDescriptorSet,
        .skinningDS      = ctx.frameBinding->skinningDescriptorSet,
    };
    if (auto step = prepareFeature(GBufferFeature{},
                                   resolved.gbuffer(),
                                   gbufferPrepare,
                                   prepared.gbuffer());
        step.isErr())
        return step.error();

    const SSAOFeature::PrepareContext ssaoPrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.ssao,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(SSAOFeature{},
                                   resolved.ssao(),
                                   ssaoPrepare,
                                   prepared.ssao());
        step.isErr())
        return step.error();

    const SkyboxFeature::PrepareContext skyboxPrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.skybox,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(SkyboxFeature{},
                                   resolved.skybox(),
                                   skyboxPrepare,
                                   prepared.skybox());
        step.isErr())
        return step.error();

    const LightingFeature::PrepareContext lightingPrepare{
        .submission            = ctx.submission,
        .stage                 = stages.light,
        .frameAndLightDS       = ctx.frameBinding->frameAndLightDescriptorSet,
        .environmentLightingDS = ctx.environmentLightingDS,
    };
    if (auto step = prepareFeature(LightingFeature{},
                                   resolved.lighting(),
                                   lightingPrepare,
                                   prepared.lighting());
        step.isErr())
        return step.error();

    const Sprite2DFeature::PrepareContext spritePrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.sprites,
        .frameData  = ctx.frameData,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(Sprite2DFeature{},
                                   resolved.sprites(),
                                   spritePrepare,
                                   prepared.sprites());
        step.isErr())
        return step.error();

    const BloomFeature::PrepareContext bloomPrepare{
        .submission = ctx.submission,
        .stage      = stages.bloom,
    };
    if (auto step = prepareFeature(BloomFeature{},
                                   resolved.bloom(),
                                   bloomPrepare,
                                   prepared.bloom());
        step.isErr())
        return step.error();

    const OverlayFeature::PrepareContext overlayPrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.overlay,
        .inputs     = ctx.overlayInputs,
        .frameData  = ctx.frameData,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(OverlayFeature{},
                                   resolved.overlay(),
                                   overlayPrepare,
                                   prepared.overlay());
        step.isErr())
        return step.error();

    const EntityIdFeature::PrepareContext entityIdPrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.entityId,
        .frameData  = ctx.frameData,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(EntityIdFeature{},
                                   resolved.entityId(),
                                   entityIdPrepare,
                                   prepared.entityId());
        step.isErr())
        return step.error();

    const FinalizeFeature::PrepareContext finalizePrepare{
        .submission = ctx.submission,
        .stage      = stages.post,
    };
    if (auto step = prepareFeature(FinalizeFeature{},
                                   resolved.finalize(),
                                   finalizePrepare,
                                   prepared.finalize());
        step.isErr())
        return step.error();

    // The extensions prepare like core features -- same step helper, same
    // failure semantics -- but their slots live in the extension tuple, so
    // adding one never edits the core schedule above.
    const SSRFeature::PrepareContext ssrPrepare{
        .submission = ctx.submission,
        .render     = ctx.render,
        .stage      = stages.ssr,
        .alignment  = ctx.alignment,
    };
    if (auto step = prepareFeature(SSRFeature{},
                                   std::get<Invocation<SSRFeature>>(resolved.extensions).resolved,
                                   ssrPrepare,
                                   std::get<Invocation<SSRFeature>>(prepared.extensions).prepared);
        step.isErr())
        return step.error();

    return prepared;
}

Result<DeferredPipelineRecipe::GraphOutputs, FeatureError>
DeferredPipelineRecipe::appendGraph(const ResolvedView&  resolved,
                                    const PreparedView&  prepared,
                                    const AppendContext& ctx)
{
    if (ctx.stages == nullptr || ctx.viewLease == nullptr || ctx.frameBinding == nullptr) {
        return FeatureError::MissingContext;
    }
    if (!resolved.gbuffer().bEnabled || !resolved.lighting().bEnabled) {
        return FeatureError::InvalidInput;
    }

    GraphOutputs outputs;

    FeatureGraphContext featureCtx;
    featureCtx.viewLease         = ctx.viewLease;
    featureCtx.stageCtx          = &ctx.stageCtx;
    featureCtx.viewId            = resolved.viewId;
    featureCtx.predecessor       = ctx.predecessor;
    featureCtx.bReverseViewportY = ctx.bReverseViewportY;

    // The append schedule: one row per connection, in run order. A row states
    // what this recipe knows -- the device object the pass draws through, the
    // typed facts, and the upstream handles the feature consumes. The step
    // helper owns the call, the failure check and the log.

    // ── Shadow (first: its passes take the family ordering point) ─────────
    if (auto step = appendFeature(ShadowFeature{},
                                  ctx.graph,
                                  ctx.stages->shadow,
                                  featureCtx,
                                  ShadowFeature::Facts{},
                                  resolved.shadow(),
                                  prepared.shadow(),
                                  ShadowFeature::Inputs{},
                                  outputs.shadow());
        step.isErr())
        return step.error();

    // ── GBuffer ───────────────────────────────────────────────────────────
    const GBufferFeature::Inputs gbufferInputs{
        .frame      = ctx.frameBuffer,
        .frameRange = prepared.frameRange,
        .light      = ctx.lightBuffer,
        .lightRange = prepared.lightRange,
        .skinning   = ctx.skinningBuffer,
    };
    if (auto step = appendFeature(GBufferFeature{},
                                  ctx.graph,
                                  ctx.stages->gBuffer,
                                  featureCtx,
                                  GBufferFeature::Facts{.viewId = resolved.viewId, .viewExtent = resolved.viewExtent},
                                  resolved.gbuffer(),
                                  prepared.gbuffer(),
                                  gbufferInputs,
                                  outputs.gbuffer());
        step.isErr())
        return step.error();

    // ── SSAO (optional) ───────────────────────────────────────────────────
    const SSAOFeature::Inputs ssaoInputs{
        .albedo = outputs.gbuffer().albedo,
        .normal = outputs.gbuffer().normal,
        .depth  = outputs.gbuffer().depth,
    };
    if (auto step = appendFeature(SSAOFeature{},
                                  ctx.graph,
                                  ctx.stages->ssao,
                                  featureCtx,
                                  SSAOFeature::Facts{.viewExtent = resolved.viewExtent},
                                  resolved.ssao(),
                                  prepared.ssao(),
                                  ssaoInputs,
                                  outputs.ssao());
        step.isErr())
        return step.error();

    // ── Lighting ──────────────────────────────────────────────────────────
    LightingFeature::Inputs lightingInputs;
    lightingInputs.albedo     = outputs.gbuffer().albedo;
    lightingInputs.normal     = outputs.gbuffer().normal;
    lightingInputs.orm        = outputs.gbuffer().orm;
    lightingInputs.shading    = outputs.gbuffer().shading;
    lightingInputs.depth      = outputs.gbuffer().depth;
    lightingInputs.frame      = ctx.frameBuffer;
    lightingInputs.frameRange = prepared.frameRange;
    lightingInputs.light      = ctx.lightBuffer;
    lightingInputs.lightRange = prepared.lightRange;
    if (outputs.ssao().ao.isValid()) {
        lightingInputs.ssao = outputs.ssao().ao.get();
    }
    if (outputs.shadow().outputs.shadowDepth.has_value()) {
        lightingInputs.shadowDepth = *outputs.shadow().outputs.shadowDepth;
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
    if (auto step = appendFeature(LightingFeature{},
                                  ctx.graph,
                                  ctx.stages->light,
                                  featureCtx,
                                  LightingFeature::Facts{.viewExtent = resolved.viewExtent},
                                  resolved.lighting(),
                                  prepared.lighting(),
                                  lightingInputs,
                                  outputs.lighting());
        step.isErr())
        return step.error();

    if (!outputs.lighting().sceneColor.isValid()) {
        YA_CORE_ERROR("Lighting append produced no SceneColor for View {}", resolved.viewId);
        return FeatureError::MissingDependency;
    }

    // ── AfterLighting extension point ─────────────────────────────────────
    // The lit colour enters as `color`; an extension may replace it. Everything
    // downstream continues whatever the stream holds after the fold, so a
    // consumer never asks whether an extension ran -- it reads the stream.
    RGTextureHandle sceneColorStream = outputs.lighting().sceneColor;
    {
        ExtensionContext extCtx{
            .graph      = ctx.graph,
            .graphCtx   = featureCtx,
            .viewExtent = resolved.viewExtent,
            .color      = sceneColorStream,
            .depth      = outputs.gbuffer().depth,
            .normal     = outputs.gbuffer().normal,
        };
        if (auto step = appendExtensionsAt<DeferredExtensionPoint::AfterLighting>(extCtx, resolved.extensions, prepared.extensions, *ctx.extensionInstances); step.isErr()) {
            return step.error();
        }
        sceneColorStream = extCtx.color;
    }

    // ── Forward-opaque slot ───────────────────────────────────────────────
    // A slot with no functional contract today: forward-rendered opaque content
    // would continue the lit color here. The pass is part of the map (kept per
    // the migration map; removing it needs separate resource-semantics
    // evidence) and is declared by the map itself — it graduates to a feature
    // once it owns workload.
    const Rect2D viewRenderArea{.pos = {0, 0}, .extent = resolved.viewExtent.toVec2()};
    (void)ctx.graph.addPass(
        makeViewGraphName(deferred_pass_names::kForwardOpaque, resolved.viewId),
        [color = sceneColorStream, depth = outputs.gbuffer().depth, viewRenderArea](RGPassBuilder& passBuilder) {
            composition::declareColorContinuation(passBuilder, viewRenderArea, color, depth);
        },
        [](RGRenderContext& rgCtx) {
            rgCtx.beginDeclaredRasterRendering();
            rgCtx.endRendering();
        });

    // ── Skybox ────────────────────────────────────────────────────────────
    const SkyboxFeature::Facts skyboxFacts{
        .viewExtent = resolved.viewExtent,
        .skybox     = ctx.overlayInputs != nullptr ? ctx.overlayInputs->skybox : ViewOverlayFrameInputs::SkyboxInput{},
    };
    const SkyboxFeature::Inputs skyboxInputs{
        .color = sceneColorStream,
        .depth = outputs.gbuffer().depth,
    };
    if (auto step = appendFeature(SkyboxFeature{},
                                  ctx.graph,
                                  ctx.stages->skybox,
                                  featureCtx,
                                  skyboxFacts,
                                  resolved.skybox(),
                                  prepared.skybox(),
                                  skyboxInputs,
                                  outputs.skybox());
        step.isErr())
        return step.error();

    // ── Scene sprites ─────────────────────────────────────────────────────
    const Sprite2DFeature::Inputs spriteInputs{
        .color = outputs.skybox().color,
        .depth = outputs.gbuffer().depth,
    };
    if (auto step = appendFeature(Sprite2DFeature{},
                                  ctx.graph,
                                  ctx.stages->sprites,
                                  featureCtx,
                                  Sprite2DFeature::Facts{
                                      .viewExtent = resolved.viewExtent,
                                      .frameData  = ctx.stageCtx.frameData,
                                  },
                                  resolved.sprites(),
                                  prepared.sprites(),
                                  spriteInputs,
                                  outputs.sprites());
        step.isErr())
        return step.error();

    // ── Bloom (optional) ──────────────────────────────────────────────────
    if (auto step = appendFeature(BloomFeature{},
                                  ctx.graph,
                                  ctx.stages->bloom,
                                  featureCtx,
                                  BloomFeature::Facts{.viewExtent = resolved.viewExtent},
                                  resolved.bloom(),
                                  prepared.bloom(),
                                  BloomFeature::Inputs{.color = outputs.sprites().color},
                                  outputs.bloom());
        step.isErr())
        return step.error();

    // ── Overlay (forward transparent) ─────────────────────────────────────
    const OverlayFeature::Inputs overlayInputs{
        .color = outputs.bloom().color,
        .depth = outputs.gbuffer().depth,
    };
    if (auto step = appendFeature(OverlayFeature{},
                                  ctx.graph,
                                  ctx.stages->overlay,
                                  featureCtx,
                                  OverlayFeature::Facts{.viewExtent = resolved.viewExtent, .inputs = ctx.overlayInputs},
                                  resolved.overlay(),
                                  prepared.overlay(),
                                  overlayInputs,
                                  outputs.overlay());
        step.isErr())
        return step.error();

    // ── Entity id (picking) ───────────────────────────────────────────────
    const EntityIdFeature::Inputs entityIdInputs{
        .depth      = outputs.gbuffer().depth,
        .skinningDS = ctx.frameBinding->skinningDescriptorSet,
    };
    if (auto step = appendFeature(EntityIdFeature{},
                                  ctx.graph,
                                  ctx.stages->entityId,
                                  featureCtx,
                                  EntityIdFeature::Facts{
                                      .viewExtent    = resolved.viewExtent,
                                      .frameData     = ctx.stageCtx.frameData,
                                      .overlayInputs = ctx.overlayInputs,
                                  },
                                  resolved.entityId(),
                                  prepared.entityId(),
                                  entityIdInputs,
                                  outputs.entityId());
        step.isErr())
        return step.error();

    // ── Finalize (mandatory display pass) ─────────────────────────────────
    if (auto step = appendFeature(FinalizeFeature{},
                                  ctx.graph,
                                  ctx.stages->post,
                                  featureCtx,
                                  FinalizeFeature::Facts{
                                      .viewExtent  = resolved.viewExtent,
                                      .postContext = ctx.postContext,
                                  },
                                  resolved.finalize(),
                                  prepared.finalize(),
                                  FinalizeFeature::Inputs{.color = outputs.overlay().color},
                                  outputs.finalize());
        step.isErr())
        return step.error();

    return outputs;
}

} // namespace ya
