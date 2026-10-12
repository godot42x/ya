#include "Render3D/Forward/Features/ForwardOpaqueFeature.h"

#include "Core/Log.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Forward/ForwardViewStage.h"

namespace ya
{

ForwardOpaqueFeature::Resolved ForwardOpaqueFeature::resolve(const Settings& settings,
                                                             const Facts&    facts,
                                                             const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    // The opaque slot always clears and always draws: its pass presence is not a
    // setting, it is where the View's color/depth stream starts.
    resolved.bEnabled    = true;
    resolved.passName    = settings.passName;
    resolved.colorFormat = settings.colorFormat;
    resolved.colorUsage  = settings.colorUsage;
    resolved.depthFormat = settings.depthFormat;
    resolved.depthUsage  = settings.depthUsage;
    return resolved;
}

Result<ForwardOpaqueFeature::Prepared, FeatureError> ForwardOpaqueFeature::prepareBindings(
    const Resolved&       resolved,
    const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.frameBinding == nullptr) {
        return FeatureError::MissingContext;
    }

    // The View binding already allocated and wrote these sets for this View; the
    // pass references them so a second consumer of the same layout cannot get a
    // different set.
    prepared.pbrFrameDescriptorSet   = ctx.frameBinding->pbrFrameDescriptorSet;
    prepared.phongFrameDescriptorSet = ctx.frameBinding->phongFrameDescriptorSet;
    prepared.unlitFrameDescriptorSet = ctx.frameBinding->unlitFrameDescriptorSet;
    prepared.skinningDescriptorSet   = ctx.frameBinding->skinningDescriptorSet;
    return prepared;
}

Result<ForwardOpaqueFeature::GraphOutputs, FeatureError> ForwardOpaqueFeature::append(
    RenderGraph&               graph,
    Stage*                     stage,
    const FeatureGraphContext& ctx,
    const Facts&               facts,
    const Resolved&            resolved,
    const Prepared&            prepared,
    const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;

    if (!resolved.bEnabled || stage == nullptr || facts.passContext == nullptr) {
        return FeatureError::MissingContext;
    }
    if (!inputs.color.isValid() || !inputs.depth.isValid()) {
        return FeatureError::MissingDependency;
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    // The View's pass context is built once by the pipeline from the View's
    // declaration; this pass stamps its own frame sets on a copy.
    ForwardViewStage::PassContext passContext = *facts.passContext;
    passContext.skinningDescriptorSet         = prepared.skinningDescriptorSet;
    passContext.pbrFrameDescriptorSet         = prepared.pbrFrameDescriptorSet;
    passContext.phongFrameDescriptorSet       = prepared.phongFrameDescriptorSet;
    passContext.unlitFrameDescriptorSet       = prepared.unlitFrameDescriptorSet;

    (void)graph.addPass(
        makeViewGraphName(resolved.passName, ctx.viewId),
        [color = inputs.color, depth = inputs.depth, renderArea, predecessor = ctx.predecessor](RGPassBuilder& passBuilder) {
            if (predecessor.has_value()) {
                passBuilder.dependsOn(*predecessor);
            }
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .clearValue  = ClearValue::black(),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ColorAttachmentOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = depth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                },
            });
        },
        [stage,
         stageCtx = *ctx.stageCtx,
         passContext,
         directionGizmos = facts.directionGizmos](RGRenderContext& rgCtx) mutable {
            const auto viewExtent = rgCtx.getRasterPassExecutionParams().getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            // The Forward stage reads the View extent off the context, as it did
            // before the pass moved here.
            RenderStageContext ctxForStage = stageCtx;
            ctxForStage.viewExtent         = viewExtent;

            stage->executePBR(ctxForStage, passContext);
            stage->executePhong(ctxForStage, passContext);
            stage->executeUnlit(ctxForStage, passContext);
            stage->executeSimple(ctxForStage, passContext);
            stage->executeDirection(ctxForStage, std::move(directionGizmos), passContext);
            stage->executeDebug(ctxForStage, passContext);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
