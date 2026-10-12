#include "Render3D/Forward/Features/ForwardSkyboxFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Core/Math/Math.h"
#include "Render3D/RenderFrameData.h"

namespace ya
{

ForwardSkyboxFeature::Resolved ForwardSkyboxFeature::resolve(const Settings& settings,
                                                             const Facts&    facts,
                                                             const Inputs&   inputs) const
{
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    resolved.passName = settings.passName;
    if (facts.frameData != nullptr) {
        resolved.frameData = ForwardViewAuxPasses::SkyboxFrameUBO{
            .proj = facts.frameData->projection,
            .view = FMath::dropTranslation(facts.frameData->view),
        };
    }
    return resolved;
}

Result<ForwardSkyboxFeature::Prepared, FeatureError> ForwardSkyboxFeature::prepareBindings(
    const Resolved&       resolved,
    const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.render == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    if (!writeUniformPassBinding(*ctx.submission,
                                 ctx.render,
                                 ctx.stage->getSkyboxFrameDSL(),
                                 ctx.alignment,
                                 &resolved.frameData,
                                 sizeof(resolved.frameData),
                                 prepared.frame)) {
        return FeatureError::AllocationFailed;
    }
    if (!prepared.frame.isValid()) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<ForwardSkyboxFeature::GraphOutputs, FeatureError> ForwardSkyboxFeature::append(
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

    if (!resolved.bEnabled || stage == nullptr || facts.skybox == nullptr || facts.skybox->mesh == nullptr) {
        return FeatureError::MissingContext;
    }
    if (!prepared.frame.isValid()) {
        return FeatureError::MissingDependency;
    }

    // The frame UBO is this feature's own host-written slice: imported here
    // rather than through the View's shared frame binding.
    const RGBufferHandle frame = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        prepared.frame.slice.buffer,
        "ForwardView.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        prepared.frame.slice.offset,
        prepared.frame.slice.size));
    const RGBufferRange frameRange{.offset = prepared.frame.slice.offset, .size = prepared.frame.slice.size};

    const Rect2D  renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    const ForwardViewAuxPasses::DrawContext::SkyboxInput skybox = *facts.skybox;

    (void)graph.addPass(
        makeViewGraphName(resolved.passName, ctx.viewId),
        [frame, frameRange, inputs, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(frame, frameRange);
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = inputs.color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = inputs.colorFinalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = inputs.depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::DepthStencilAttachmentOptimal,
                },
            });
        },
        [stageCtx = *ctx.stageCtx, skybox, frameDS = prepared.frame.set, stage, bReverseViewportY = ctx.bReverseViewportY](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            stage->drawSkybox(ForwardViewAuxPasses::DrawContext{
                .stageCtx                 = stageCtx,
                .skybox                   = skybox,
                .skyboxFrameDescriptorSet = frameDS,
                .bReverseViewportY        = bReverseViewportY,
            });
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
