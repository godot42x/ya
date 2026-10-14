#include "Render3D/Forward/ForwardPipelineRecipe.h"

#include "Core/Log.h"

#include <format>
#include <string>
#include "Core/Math/Math.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Forward/ForwardPassNames.h"

namespace ya
{

std::vector<ViewAttachmentDesc> ForwardPipelineRecipe::ResolvedView::collectViewTargets() const
{
    std::vector<ViewAttachmentDesc> targets;
    targets.reserve(7);
    foldTargets(invocations, targets);
    return targets;
}

std::string ForwardPipelineRecipe::ResolvedView::describe() const
{
    std::string out = std::format("Forward View {} ({}x{})\n", viewId, viewExtent.width, viewExtent.height);
    foldDescribe(invocations, out);

    out += std::format("  targets: {}", collectViewTargets().size());
    for (const ViewAttachmentDesc& target : collectViewTargets()) {
        out += std::format(" {}", static_cast<uint32_t>(target.role));
    }
    out += "\n";
    return out;
}

Result<std::vector<ForwardPipelineRecipe::ResolvedView>, FeatureError>
ForwardPipelineRecipe::resolve(const Config& config, const RenderFramePlan& plan)
{
    if (!config.bEnabled) {
        return FeatureError::InvalidInput;
    }

    const ShadowFeature shadowFeature;

    ForwardOpaqueFeature::Settings opaqueSettings;
    opaqueSettings.passName    = forward_pass_names::kOpaque;
    opaqueSettings.colorFormat = config.viewColorFormat;
    opaqueSettings.colorUsage  = config.viewColorUsage;
    opaqueSettings.depthFormat = config.viewDepthFormat;
    opaqueSettings.depthUsage  = config.viewDepthUsage;

    ForwardSkyboxFeature::Settings skyboxSettings;
    skyboxSettings.passName = forward_pass_names::kSkybox;

    EntityIdFeature::Settings entityIdSettings;
    entityIdSettings.passName = forward_pass_names::kEntityId;
    entityIdSettings.importLabel = forward_pass_names::kEntityIdImport;
    // Forward keeps the View's depth attachable: the passes after the id pass
    // still attach it, and the View is built against that layout.
    entityIdSettings.depthFinalLayout = EImageLayout::DepthStencilAttachmentOptimal;

    BloomFeature::Settings bloomSettings = config.bloom;
    bloomSettings.importLabelPrefix      = forward_pass_names::kBloomImportPrefix;

    const ForwardOpaqueFeature opaqueFeature;
    const ForwardSkyboxFeature skyboxFeature;
    const EntityIdFeature      entityIdFeature;
    const BloomFeature         bloomFeature;
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

        view.opaque() = opaqueFeature.resolve(
            opaqueSettings,
            ForwardOpaqueFeature::Facts{.viewExtent = view.viewExtent},
            ForwardOpaqueFeature::Inputs{});

        view.skybox() = skyboxFeature.resolve(
            skyboxSettings,
            ForwardSkyboxFeature::Facts{
                .viewExtent = view.viewExtent,
                .frameData  = recording.frameData,
            },
            ForwardSkyboxFeature::Inputs{});

        view.entityId() = entityIdFeature.resolve(
            entityIdSettings,
            EntityIdFeature::Facts{.viewExtent = view.viewExtent},
            EntityIdFeature::Inputs{});

        view.bloom() = bloomFeature.resolve(
            bloomSettings,
            BloomFeature::Facts{.viewExtent = view.viewExtent},
            BloomFeature::Inputs{});

        view.finalize() = finalizeFeature.resolve(
            config.finalize,
            FinalizeFeature::Facts{.viewExtent = view.viewExtent},
            FinalizeFeature::Inputs{});

        resolved.push_back(view);
    }
    return resolved;
}

Result<ForwardPipelineRecipe::PreparedView, FeatureError>
ForwardPipelineRecipe::prepareBindings(const ResolvedView&   resolved,
                                       const Stages&         stages,
                                       const PrepareContext& ctx)
{
    if (ctx.submission == nullptr || ctx.frameBinding == nullptr) {
        return FeatureError::MissingContext;
    }

    // The prepare schedule: one row per feature, in map order (see the Deferred
    // recipe's prepareBindings for the shape).
    PreparedView prepared;
    prepared.viewId = resolved.viewId;

    if (auto step = prepareFeature(ShadowFeature{}, resolved.shadow(), ShadowFeature::PrepareContext{.submission = ctx.submission, .stage = stages.shadow, .view = ctx.view}, prepared.shadow()); step.isErr())
        return step.error();

    if (auto step = prepareFeature(ForwardOpaqueFeature{}, resolved.opaque(), ForwardOpaqueFeature::PrepareContext{.frameBinding = ctx.frameBinding, .submission = ctx.submission, .render = ctx.render, .auxPasses = ctx.auxPasses, .alignment = ctx.alignment, .frameData = ctx.frameData, .viewExtent = resolved.viewExtent}, prepared.opaque()); step.isErr())
        return step.error();

    if (auto step = prepareFeature(ForwardSkyboxFeature{}, resolved.skybox(), ForwardSkyboxFeature::PrepareContext{.submission = ctx.submission, .render = ctx.render, .stage = stages.viewStage != nullptr ? &stages.viewStage->getAuxPasses() : nullptr, .alignment = ctx.alignment}, prepared.skybox()); step.isErr())
        return step.error();

    if (auto step = prepareFeature(EntityIdFeature{}, resolved.entityId(), EntityIdFeature::PrepareContext{.submission = ctx.submission, .render = ctx.render, .stage = stages.entityId, .frameData = ctx.frameData, .alignment = ctx.alignment}, prepared.entityId()); step.isErr())
        return step.error();

    if (auto step = prepareFeature(BloomFeature{}, resolved.bloom(), BloomFeature::PrepareContext{.submission = ctx.submission, .stage = stages.bloom}, prepared.bloom()); step.isErr())
        return step.error();

    if (auto step = prepareFeature(FinalizeFeature{}, resolved.finalize(), FinalizeFeature::PrepareContext{.submission = ctx.submission, .stage = stages.post}, prepared.finalize()); step.isErr())
        return step.error();

    return prepared;
}

Result<ForwardPipelineRecipe::GraphOutputs, FeatureError>
ForwardPipelineRecipe::appendGraph(const ResolvedView&  resolved,
                                   const PreparedView&  prepared,
                                   const AppendContext& ctx)
{
    if (ctx.stages == nullptr || ctx.viewLease == nullptr || ctx.frameBinding == nullptr) {
        return FeatureError::MissingContext;
    }

    GraphOutputs outputs;

    FeatureGraphContext featureCtx;
    featureCtx.viewLease         = ctx.viewLease;
    featureCtx.stageCtx          = &ctx.stageCtx;
    featureCtx.viewId            = resolved.viewId;
    featureCtx.predecessor       = ctx.predecessor;
    featureCtx.bReverseViewportY = ctx.bReverseViewportY;

    // The View's stream starts here: the opaque pass clears color and depth.
    const std::shared_ptr<RenderTexture> colorTarget = ctx.viewLease->find(EViewAttachment::SceneColor);
    const std::shared_ptr<RenderTexture> depthTarget = ctx.viewLease->find(EViewAttachment::SceneDepth);
    if (colorTarget == nullptr || depthTarget == nullptr) {
        YA_CORE_ERROR("Forward View {} has no prepared color/depth targets", resolved.viewId);
        return FeatureError::MissingDependency;
    }
    const RGImportScope importScope(ctx.graph, resolved.viewId);
    const RGTextureHandle color = ctx.graph.importTexture(makeImportedTextureDesc(
        *colorTarget, forward_graph_exports::viewColor, EImageLayout::ColorAttachmentOptimal, resolved.opaque().colorUsage));
    const RGTextureHandle depth = ctx.graph.importTexture(makeImportedTextureDesc(
        *depthTarget, forward_graph_exports::viewDepth, EImageLayout::DepthStencilAttachmentOptimal, resolved.opaque().depthUsage));

    // The append schedule: one row per connection, in run order (see the
    // Deferred recipe's appendGraph for the shape).

    // ── Shadow (first: its passes take the family ordering point) ─────────
    if (auto step = appendFeature(ShadowFeature{}, ctx.graph, ctx.stages->shadow, featureCtx, ShadowFeature::Facts{}, resolved.shadow(), prepared.shadow(), ShadowFeature::Inputs{}, outputs.shadow()); step.isErr())
        return step.error();

    // ── Opaque (clear + every shading model) ──────────────────────────────
    // No stage guard here: the feature itself answers MissingContext when the
    // view stage is absent, so the row can be a plain call.
    if (auto step = appendFeature(ForwardOpaqueFeature{}, ctx.graph, ctx.stages->viewStage, featureCtx, ForwardOpaqueFeature::Facts{.viewExtent = resolved.viewExtent, .passContext = ctx.viewPassContext, .directionGizmos = ctx.directionGizmos}, resolved.opaque(), prepared.opaque(), ForwardOpaqueFeature::Inputs{.color = color, .depth = depth}, outputs.opaque()); step.isErr())
        return step.error();
    // The depth stream is the recipe's own import; it belongs in the typed
    // outputs the collector reads, beside the color the opaque pass produced.
    outputs.opaque().depth = depth;

    // ── Skybox ────────────────────────────────────────────────────────────
    // Without a view stage there is nothing to draw the skybox through, and the
    // color simply continues: this is a legal skip, not a failure.
    if (ctx.stages->viewStage == nullptr) {
        outputs.skybox().color = outputs.opaque().color;
    } else if (auto step = appendFeature(ForwardSkyboxFeature{}, ctx.graph, &ctx.stages->viewStage->getAuxPasses(), featureCtx, ForwardSkyboxFeature::Facts{.viewExtent = resolved.viewExtent, .skybox = ctx.skybox}, resolved.skybox(), prepared.skybox(), ForwardSkyboxFeature::Inputs{.color = outputs.opaque().color, .depth = depth, .colorFinalLayout = EImageLayout::ColorAttachmentOptimal}, outputs.skybox());
               step.isErr()) {
        return step.error();
    }

    // ── Transparent slot ──────────────────────────────────────────────────
    // A slot with no functional contract today: forward-rendered transparent
    // content would continue the color here. The pass is part of the map (kept
    // per the migration map) and is declared by the map itself — it graduates
    // to a feature once it owns workload.
    const Rect2D viewRenderArea{.pos = {0, 0}, .extent = resolved.viewExtent.toVec2()};
    (void)ctx.graph.addPass(
        makeViewGraphName(forward_pass_names::kTransparent, resolved.viewId),
        [colorStream = outputs.skybox().color, depthStream = depth, viewRenderArea](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = viewRenderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = colorStream,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ColorAttachmentOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depthStream,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                },
            });
        },
        [](RGRenderContext& rgCtx) {
            rgCtx.beginDeclaredRasterRendering();
            rgCtx.endRendering();
        });

    // ── Entity id (picking) ───────────────────────────────────────────────
    if (auto step = appendFeature(EntityIdFeature{}, ctx.graph, ctx.stages->entityId, featureCtx, EntityIdFeature::Facts{.viewExtent = resolved.viewExtent, .frameData = ctx.stageCtx.frameData}, resolved.entityId(), prepared.entityId(), EntityIdFeature::Inputs{.depth = depth, .skinningDS = ctx.frameBinding->skinningDescriptorSet}, outputs.entityId()); step.isErr())
        return step.error();

    // ── Bloom (optional) ──────────────────────────────────────────────────
    if (auto step = appendFeature(BloomFeature{}, ctx.graph, ctx.stages->bloom, featureCtx, BloomFeature::Facts{.viewExtent = resolved.viewExtent}, resolved.bloom(), prepared.bloom(), BloomFeature::Inputs{.color = outputs.skybox().color}, outputs.bloom()); step.isErr())
        return step.error();

    // ── Finalize (mandatory display pass) ─────────────────────────────────
    if (auto step = appendFeature(FinalizeFeature{}, ctx.graph, ctx.stages->post, featureCtx, FinalizeFeature::Facts{.viewExtent = resolved.viewExtent, .postContext = ctx.postContext}, resolved.finalize(), prepared.finalize(), FinalizeFeature::Inputs{.color = outputs.bloom().color}, outputs.finalize()); step.isErr())
        return step.error();

    return outputs;
}

} // namespace ya
