#include "Render3D/Deferred/Features/SkyboxFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Composition/ColorContinuation.h"
#include "Render3D/Deferred/DeferredPassNames.h"

namespace ya
{

void SkyboxFeature::init(IRender* render)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<SkyboxStage>();
    }
    _stage->init(render);
}

void SkyboxFeature::shutdown()
{
    if (_stage) {
        _stage->destroy();
        _stage.reset();
    }
}

void SkyboxFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

void SkyboxFeature::refreshDeviceFormats(const DeferredAttachmentFormats& formats)
{
    if (_stage) {
        _stage->refreshPipelineFormats(formats);
    }
}

SkyboxFeature::Resolved SkyboxFeature::resolve(const Settings& settings,
                                               const Facts&    facts,
                                               const Inputs&   inputs) const
{
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    if (facts.frameData != nullptr) {
        resolved.frameData = SkyboxStage::buildFrameData(*facts.frameData);
    }
    return resolved;
}

Result<SkyboxFeature::Prepared, FeatureError> SkyboxFeature::prepareBindings(const Resolved&       resolved,
                                                                             const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.render == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    prepared.bEnabled = true;
    if (!writeUniformPassBinding(*ctx.submission,
                                 ctx.render,
                                 ctx.stage->getFrameDSL(),
                                 ctx.alignment,
                                 &resolved.frameData,
                                 sizeof(resolved.frameData),
                                 prepared.frame)) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<SkyboxFeature::GraphOutputs, FeatureError> SkyboxFeature::append(RenderGraph&               graph,
                                                                       Stage*                     stage,
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

    // The frame UBO is this feature's own host-written slice: imported here
    // rather than through the View's shared frame binding.
    const RGBufferHandle frame = graph.importBuffer(makeHostWrittenImportedBufferDesc(
        prepared.frame.slice.buffer,
        "Deferred.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        prepared.frame.slice.offset,
        prepared.frame.slice.size));
    const RGBufferRange frameRange{.offset = prepared.frame.slice.offset, .size = prepared.frame.slice.size};

    const Rect2D renderArea{.pos = {0, 0}, .extent = facts.viewExtent.toVec2()};
    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kSkybox, ctx.viewId),
        [frame, frameRange, inputs, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(frame, frameRange);
            composition::declareColorContinuation(passBuilder, renderArea, inputs.color, inputs.depth);
        },
        [stageCtx = *ctx.stageCtx, skybox = facts.skybox, frameDS = prepared.frame.set, stage](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();
            stage->executeSkybox(stageCtx, skybox, frameDS);
            rgCtx.endRendering();
        });

    return outputs;
}

} // namespace ya
