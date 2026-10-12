#include "Render3D/Deferred/Features/ForwardOpaqueFeature.h"

#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredPassNames.h"

namespace ya
{

ForwardOpaqueFeature::Resolved ForwardOpaqueFeature::resolve(const Settings& settings,
                                                             const Facts&    facts,
                                                             const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    return resolved;
}

ForwardOpaqueFeature::GraphOutputs ForwardOpaqueFeature::append(RenderGraph&               graph,
                                                                const FeatureGraphContext& ctx,
                                                                const Facts&               facts,
                                                                const Resolved&            resolved,
                                                                const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;
    if (!resolved.bEnabled) {
        return outputs;
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kForwardOpaque, ctx.viewId),
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
                },
            });
        },
        [](RGRenderContext& rgCtx) {
            rgCtx.beginDeclaredRasterRendering();
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
