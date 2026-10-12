#include "Render3D/Deferred/Features/OverlayFeature.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Composition/ColorContinuation.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/RenderFrameData.h"

namespace ya
{

OverlayFeature::Resolved OverlayFeature::resolve(const Settings& settings,
                                                 const Facts&    facts,
                                                 const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    // Resolve sees no graph handles (they do not exist at the safe point): the
    // pass is part of the map, its content decides the draws.
    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    return resolved;
}

Result<OverlayFeature::Prepared, FeatureError> OverlayFeature::prepareBindings(const Resolved&       resolved,
                                                                               const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr || ctx.inputs == nullptr || ctx.frameData == nullptr) {
        return FeatureError::MissingContext;
    }

    const ViewOverlayStage::BillboardFrameUBO ubo{
        .viewProjection = ctx.frameData->viewProjection,
        .view           = ctx.frameData->view,
    };
    if (!writeUniformPassBinding(*ctx.submission,
                                 ctx.render,
                                 ctx.stage->getBillboardFrameDSL(),
                                 ctx.alignment,
                                 &ubo,
                                 sizeof(ubo),
                                 prepared.bindings.billboardFrame)) {
        return FeatureError::AllocationFailed;
    }
    prepared.bindings.billboardTextures.set = allocateCombinedImageSamplerSet(
        *ctx.submission, ctx.stage->getBillboardTextureDSL(), ViewOverlayStage::kBillboardTextureCount);
    if (!prepared.bindings.billboardTextures.set) {
        return FeatureError::AllocationFailed;
    }
    ctx.stage->updateBillboardTextures(*ctx.inputs, prepared.bindings);
    return prepared;
}

Result<OverlayFeature::GraphOutputs, FeatureError> OverlayFeature::append(RenderGraph&               graph,
                                                                           Stage*                     stage,
                                                                           const FeatureGraphContext& ctx,
                                                                           const Facts&               facts,
                                                                           const Resolved&            resolved,
                                                                           const Prepared&            prepared,
                                                                           const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;
    if (!resolved.bEnabled || stage == nullptr || facts.inputs == nullptr) {
        return outputs;
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    const ViewOverlayStage::FrameInputs frameInputs = *facts.inputs;
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kForwardTransparent, ctx.viewId),
        [inputs, renderArea](RGPassBuilder& passBuilder) {
            composition::declareColorContinuation(passBuilder, renderArea, inputs.color, inputs.depth);
        },
        [stageCtx = *ctx.stageCtx, frameInputs, bindings = prepared.bindings, stage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
            stage->executeOverlay(stageCtx, frameInputs, bindings);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
