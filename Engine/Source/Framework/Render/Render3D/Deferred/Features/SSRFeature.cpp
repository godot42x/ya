#include "Render3D/Deferred/Features/SSRFeature.h"

#include "Core/Log.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "RHI/Render.h"

namespace ya
{

SSRFeature::Resolved SSRFeature::resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const
{
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    if (!resolved.bEnabled) {
        return resolved;
    }

    if (facts.frameData != nullptr) {
        resolved.frameData = SSRStage::buildFrameData(*facts.frameData,
                                                      facts.viewExtent,
                                                      settings.maxDistance,
                                                      settings.thickness,
                                                      settings.intensity,
                                                      settings.stepCount,
                                                      facts.bReverseY);
    }
    resolved.outputFormat = SSRStage::OUTPUT_FORMAT;
    return resolved;
}

Result<SSRFeature::Prepared, FeatureError> SSRFeature::prepareBindings(const Resolved&       resolved,
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
    prepared.inputDS = allocateCombinedImageSamplerSet(*ctx.submission, ctx.stage->getInputDSL(), 3);
    if (!prepared.frame.isValid() || !prepared.inputDS) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<void, FeatureError> SSRFeature::appendExtension(ExtensionContext& ctx,
                                                       const Resolved&   resolved,
                                                       const Prepared&   prepared) const
{
    // Disabled: the colour passes through unchanged. A consumer of this
    // extension point never has to ask whether SSR ran.
    if (!resolved.bEnabled) {
        return Result<void, FeatureError>::success();
    }
    if (_stage == nullptr || !prepared.frame.isValid() || !ctx.color.isValid()) {
        return FeatureError::MissingContext;
    }
    SSRStage* stage = _stage.get();

    // The reflection result is this feature's own transient: it is not a View
    // attachment, so no shared role is added for one effect's private scratch.
    const RGTextureHandle reflection = ctx.graph.createTexture(RGTextureDesc{
        .label  = "SSR.Reflection",
        .format = resolved.outputFormat,
        .extent = Extent3D{ctx.viewExtent.width, ctx.viewExtent.height, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    const RGBufferHandle frame = ctx.graph.importBuffer(makeHostWrittenImportedBufferDesc(
        prepared.frame.slice.buffer,
        "Deferred.SSRFrameUBO",
        EBufferUsage::UniformBuffer,
        prepared.frame.slice.offset,
        prepared.frame.slice.size));
    const RGBufferRange frameRange{.offset = prepared.frame.slice.offset, .size = prepared.frame.slice.size};

    const RGTextureHandle sceneColor = ctx.color;
    const RGTextureHandle normal     = ctx.normal;
    const RGTextureHandle depth      = ctx.depth;
    const Rect2D renderArea{.pos = {0.0f, 0.0f}, .extent = glm::vec2(ctx.viewExtent.width, ctx.viewExtent.height)};

    (void)ctx.graph.addPass(
        makeViewGraphName(deferred_pass_names::kSSR, ctx.viewId()),
        [frame, frameRange, sceneColor, normal, depth, reflection, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(frame, frameRange);
            passBuilder.read(sceneColor);
            passBuilder.read(normal);
            passBuilder.read(depth);
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = reflection,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stage, prepared, sceneColor, normal, depth](RGRenderContext& rgCtx) {
            stage->writeInputDescriptors(
                prepared.inputDS,
                rgCtx.getBindingContext(),
                sceneColor,
                normal,
                depth);

            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            rgCtx.getCommandBuffer().bindPipeline(stage->getPipeline());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
            rgCtx.getCommandBuffer().setScissor(0, 0, renderExtent.width, renderExtent.height);
            rgCtx.getCommandBuffer().bindDescriptorSets(
                stage->getPipelineLayout(), 0, {prepared.frame.set, prepared.inputDS});
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });

    // The extension replaces the colour stream: every later point reads this.
    ctx.color = reflection;
    return Result<void, FeatureError>::success();
}

void SSRFeature::init(const FeatureInitContext& ctx)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<SSRStage>();
    }
    _stage->init(ctx.render);
}

void SSRFeature::shutdown()
{
    if (_stage) {
        _stage->destroy();
        _stage.reset();
    }
}

void SSRFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

void SSRFeature::refreshDeviceFormats(EFormat::T colorFormat)
{
    if (_stage) {
        _stage->refreshPipelineFormat(colorFormat);
    }
}

} // namespace ya
