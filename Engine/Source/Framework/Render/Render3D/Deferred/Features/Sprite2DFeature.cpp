#include "Render3D/Deferred/Features/Sprite2DFeature.h"

#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/Sprite2DStage.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "Render3D/RenderFrameData.h"

namespace ya
{

Sprite2DFeature::Resolved Sprite2DFeature::resolve(const Settings& settings,
                                                   const Facts&    facts,
                                                   const Inputs&   inputs) const
{
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled && facts.frameData != nullptr && !facts.frameData->worldSprites.empty();
    return resolved;
}

Result<Sprite2DFeature::Prepared, FeatureError> Sprite2DFeature::prepareBindings(const Resolved&       resolved,
                                                                                 const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr || ctx.frameData == nullptr) {
        return FeatureError::MissingContext;
    }

    const Sprite2DStage::FrameData frame{.viewProj = ctx.frameData->viewProjection};
    if (!writeUniformPassBinding(*ctx.submission,
                                 ctx.render,
                                 ctx.stage->getFrameDSL(),
                                 ctx.alignment,
                                 &frame,
                                 sizeof(frame),
                                 prepared.bindings.frame)) {
        return FeatureError::AllocationFailed;
    }
    ctx.stage->updateTextures(*ctx.submission, *ctx.frameData, prepared.bindings);
    return prepared;
}

Sprite2DFeature::GraphOutputs Sprite2DFeature::append(RenderGraph&               graph,
                                                      Sprite2DStage*             stage,
                                                      const FeatureGraphContext& ctx,
                                                      const Facts&               facts,
                                                      const Resolved&            resolved,
                                                      const Prepared&            prepared,
                                                      const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;
    if (!resolved.bEnabled || stage == nullptr) {
        return outputs;
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kSprites, ctx.viewId),
        [inputs, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = inputs.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = inputs.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    .bReadOnly   = true,
                },
            });
        },
        [stageCtx = *ctx.stageCtx, bindings = prepared.bindings, stage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            stage->executeSprites(stageCtx, bindings);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
