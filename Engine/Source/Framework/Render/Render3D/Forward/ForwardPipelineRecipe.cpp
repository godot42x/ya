#include "Render3D/Forward/ForwardPipelineRecipe.h"

#include "Core/Log.h"

#include <format>
#include <string>
#include "Core/Math/Math.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Forward/ForwardPassNames.h"

namespace ya
{

namespace
{

template <typename... F>
void foldTargets(const std::tuple<Invocation<F>...>& invocations, std::vector<ViewAttachmentDesc>& out)
{
    (..., F::targets(std::get<Invocation<F>>(invocations).resolved, out));
}

template <typename... F>
void foldDescribe(const std::tuple<Invocation<F>...>& invocations, std::string& out)
{
    // No lambda: the fold body runs as a plain comma expression so there is no
    // capture to get wrong and nothing can swallow the expansion.
    ((out += std::format("  {} {}\n",
                         std::get<Invocation<F>>(invocations).resolved.bEnabled ? "[on] " : "[off]",
                         F::name())), ...);
}

} // namespace

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

    const ShadowFeature shadowFeature;
    auto shadowPrepared = shadowFeature.prepareBindings(
        resolved.shadow(),
        ShadowFeature::PrepareContext{
            .submission = ctx.submission,
            .stage      = stages.shadow,
            .view       = ctx.view,
        });
    if (shadowPrepared.isErr()) {
        return shadowPrepared.error();
    }

    PreparedView prepared;
    prepared.viewId = resolved.viewId;
    prepared.shadow() = shadowPrepared.value();

    const ForwardOpaqueFeature opaqueFeature;
    auto opaquePrepared = opaqueFeature.prepareBindings(
        resolved.opaque(),
        ForwardOpaqueFeature::PrepareContext{
            .frameBinding = ctx.frameBinding,
            .submission   = ctx.submission,
            .render       = ctx.render,
            .auxPasses    = ctx.auxPasses,
            .alignment    = ctx.alignment,
            .frameData    = ctx.frameData,
            .viewExtent   = resolved.viewExtent,
        });
    if (opaquePrepared.isErr()) {
        return opaquePrepared.error();
    }
    prepared.opaque() = opaquePrepared.value();

    const ForwardSkyboxFeature skyboxFeature;
    auto skyboxPrepared = skyboxFeature.prepareBindings(
        resolved.skybox(),
        ForwardSkyboxFeature::PrepareContext{
            .submission = ctx.submission,
            .render     = ctx.render,
            .stage      = stages.viewStage != nullptr ? &stages.viewStage->getAuxPasses() : nullptr,
            .alignment  = ctx.alignment,
        });
    if (skyboxPrepared.isErr()) {
        return skyboxPrepared.error();
    }
    prepared.skybox() = skyboxPrepared.value();

    const EntityIdFeature entityIdFeature;
    auto entityIdPrepared = entityIdFeature.prepareBindings(
        resolved.entityId(),
        EntityIdFeature::PrepareContext{
            .submission = ctx.submission,
            .render     = ctx.render,
            .stage      = stages.entityId,
            .frameData  = ctx.frameData,
            .alignment  = ctx.alignment,
        });
    if (entityIdPrepared.isErr()) {
        return entityIdPrepared.error();
    }
    prepared.entityId() = entityIdPrepared.value();

    const BloomFeature bloomFeature;
    auto bloomPrepared = bloomFeature.prepareBindings(
        resolved.bloom(),
        BloomFeature::PrepareContext{
            .submission = ctx.submission,
            .stage      = stages.post,
        });
    if (bloomPrepared.isErr()) {
        return bloomPrepared.error();
    }
    prepared.bloom() = bloomPrepared.value();

    const FinalizeFeature finalizeFeature;
    auto finalizePrepared = finalizeFeature.prepareBindings(
        resolved.finalize(),
        FinalizeFeature::PrepareContext{
            .submission = ctx.submission,
            .stage      = stages.post,
        });
    if (finalizePrepared.isErr()) {
        return finalizePrepared.error();
    }
    prepared.finalize() = finalizePrepared.value();

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

    // ── Shadow (first: its passes take the family ordering point) ─────────
    const ShadowFeature shadowFeature;
    auto shadowOutputs = shadowFeature.append(
        ctx.graph,
        ctx.stages->shadow,
        featureCtx,
        ShadowFeature::Facts{},
        resolved.shadow(),
        prepared.shadow(),
        ShadowFeature::Inputs{});
    if (shadowOutputs.isErr()) {
        YA_CORE_ERROR("Forward shadow append failed for View {}", resolved.viewId);
        return shadowOutputs.error();
    }
    outputs.shadow() = shadowOutputs.value();

    // ── Opaque (clear + every shading model) ──────────────────────────────
    auto opaqueOutputs = ctx.stages->viewStage != nullptr
        ? ForwardOpaqueFeature{}.append(
              ctx.graph,
              ctx.stages->viewStage,
              featureCtx,
              ForwardOpaqueFeature::Facts{
                  .viewExtent      = resolved.viewExtent,
                  .passContext     = ctx.viewPassContext,
                  .directionGizmos = ctx.directionGizmos,
              },
              resolved.opaque(),
              prepared.opaque(),
              ForwardOpaqueFeature::Inputs{.color = color, .depth = depth})
        : Result<ForwardOpaqueFeature::GraphOutputs, FeatureError>{FeatureError::MissingContext};
    if (opaqueOutputs.isErr()) {
        YA_CORE_ERROR("Forward opaque append failed for View {}", resolved.viewId);
        return opaqueOutputs.error();
    }
    outputs.opaque() = opaqueOutputs.value();
    // The depth stream is the recipe's own import; it belongs in the typed
    // outputs the collector reads, beside the color the opaque pass produced.
    outputs.opaque().depth = depth;

    // ── Skybox ────────────────────────────────────────────────────────────
    if (ctx.stages->viewStage != nullptr) {
        auto skyboxOutputs = ForwardSkyboxFeature{}.append(
            ctx.graph,
            &ctx.stages->viewStage->getAuxPasses(),
            featureCtx,
            ForwardSkyboxFeature::Facts{
                .viewExtent = resolved.viewExtent,
                .skybox     = ctx.skybox,
            },
            resolved.skybox(),
            prepared.skybox(),
            ForwardSkyboxFeature::Inputs{
                .color           = outputs.opaque().color,
                .depth           = depth,
                .colorFinalLayout = EImageLayout::ColorAttachmentOptimal,
            });
        if (skyboxOutputs.isErr()) {
            YA_CORE_ERROR("Forward skybox append failed for View {}", resolved.viewId);
            return skyboxOutputs.error();
        }
        outputs.skybox() = skyboxOutputs.value();
    }
    else {
        outputs.skybox().color = outputs.opaque().color;
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
    auto entityIdOutputs = ctx.stages->entityId != nullptr
        ? EntityIdFeature{}.append(
              ctx.graph,
              ctx.stages->entityId,
              featureCtx,
              EntityIdFeature::Facts{
                  .viewExtent = resolved.viewExtent,
                  .frameData  = ctx.stageCtx.frameData,
              },
              resolved.entityId(),
              prepared.entityId(),
              EntityIdFeature::Inputs{
                  .depth      = depth,
                  .skinningDS = ctx.frameBinding->skinningDescriptorSet,
              })
        : Result<EntityIdFeature::GraphOutputs, FeatureError>{FeatureError::MissingContext};
    if (entityIdOutputs.isErr()) {
        YA_CORE_ERROR("Forward entity id append failed for View {}", resolved.viewId);
        return entityIdOutputs.error();
    }
    outputs.entityId() = entityIdOutputs.value();

    // ── Bloom (optional) ──────────────────────────────────────────────────
    const BloomFeature bloomFeature;
    auto bloomOutputs = bloomFeature.append(
        ctx.graph,
        ctx.stages->post,
        featureCtx,
        BloomFeature::Facts{.viewExtent = resolved.viewExtent},
        resolved.bloom(),
        prepared.bloom(),
        BloomFeature::Inputs{.color = outputs.skybox().color});
    if (bloomOutputs.isErr()) {
        YA_CORE_ERROR("Forward bloom append failed for View {}", resolved.viewId);
        return bloomOutputs.error();
    }
    outputs.bloom() = bloomOutputs.value();

    // ── Finalize (mandatory display pass) ─────────────────────────────────
    auto finalizeOutputs = FinalizeFeature{}.append(
        ctx.graph,
        ctx.stages->post,
        featureCtx,
        FinalizeFeature::Facts{.viewExtent = resolved.viewExtent, .postContext = ctx.postContext},
        resolved.finalize(),
        prepared.finalize(),
        FinalizeFeature::Inputs{.color = outputs.bloom().color});
    if (finalizeOutputs.isErr()) {
        YA_CORE_ERROR("Forward finalize append failed for View {}", resolved.viewId);
        return finalizeOutputs.error();
    }
    outputs.finalize() = finalizeOutputs.value();

    return outputs;
}

} // namespace ya
