#include "Render3D/Deferred/Features/SkyboxFeature.h"

#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Composition/ColorContinuation.h"
#include "Render3D/Deferred/DeferredPassNames.h"

namespace ya
{

SkyboxFeature::Resolved SkyboxFeature::resolve(const Settings& settings,
                                               const Facts&    facts,
                                               const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    return resolved;
}

SkyboxFeature::GraphOutputs SkyboxFeature::append(RenderGraph&               graph,
                                                  ViewOverlayStage*          stage,
                                                  const FeatureGraphContext& ctx,
                                                  const Facts&               facts,
                                                  const Resolved&            resolved,
                                                  const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;
    if (!resolved.bEnabled || stage == nullptr) {
        return outputs;
    }

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kSkybox, ctx.viewId),
        [inputs, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(inputs.frame, inputs.frameRange);
            composition::declareColorContinuation(passBuilder, renderArea, inputs.color, inputs.depth);
        },
        [stageCtx = *ctx.stageCtx, skybox = facts.skybox, stage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            stage->executeSkybox(stageCtx, skybox);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
