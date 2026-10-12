#include "Render3D/Deferred/Features/SSAOFeature.h"

#include "Core/Log.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Deferred/DeferredPassNames.h"
#include "RHI/Backend/TextureLibrary.h"

#include <glm/glm.hpp>

namespace ya
{

SSAOFeature::Resolved SSAOFeature::resolve(const Settings& settings,
                                           const Facts&    facts,
                                           const Inputs&   inputs) const
{
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    resolved.settings = settings;
    if (!resolved.bEnabled) {
        return resolved;
    }

    resolved.aoTarget = ViewTargetRequirement{
        .role   = EViewAttachment::SSAO,
        .format = SSAOStage::AO_FORMAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };

    if (facts.frameData != nullptr) {
        resolved.frameData = SSAOStage::buildFrameData(*facts.frameData,
                                                       facts.viewExtent,
                                                       settings.radius,
                                                       settings.bias,
                                                       settings.power,
                                                       settings.intensity,
                                                       facts.bReverseY);
    }
    return resolved;
}

Result<SSAOFeature::Prepared, FeatureError> SSAOFeature::prepareBindings(const Resolved&       resolved,
                                                                         const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    prepared.bEnabled = true;
    prepared.frameDS  = ctx.frameDS;
    prepared.inputDS  = allocateCombinedImageSamplerSet(*ctx.submission, ctx.stage->getInputDSL(), 4);
    if (!prepared.frameDS || !prepared.inputDS) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<SSAOFeature::GraphOutputs, FeatureError> SSAOFeature::append(RenderGraph&               graph,
                                                                    SSAOStage*                 stage,
                                                                    const FeatureGraphContext& ctx,
                                                                    const Facts&               facts,
                                                                    const Resolved&            resolved,
                                                                    const Prepared&            prepared,
                                                                    const Inputs&              inputs) const
{
    GraphOutputs outputs;
    if (!resolved.bEnabled) {
        return outputs;
    }
    if (stage == nullptr || ctx.viewLease == nullptr || !resolved.aoTarget.has_value()) {
        return FeatureError::MissingContext;
    }

    const std::shared_ptr<RenderTexture> target = ctx.viewLease->find(resolved.aoTarget->role);
    if (target == nullptr) {
        return FeatureError::MissingDependency;
    }
    const RGTextureHandle output = graph.importTexture(makeImportedTextureDesc(
        *target, "Deferred.SSAO", EImageLayout::ShaderReadOnlyOptimal, resolved.aoTarget->usage));

    const stdptr<Texture>& noiseTexture = stage->getNoiseTexture();
    if (!noiseTexture) {
        return FeatureError::MissingDependency;
    }
    const RGTextureHandle noise = graph.importTexture(
        makeImportedTextureDesc(*noiseTexture, "SSAO.Noise", EImageLayout::ShaderReadOnlyOptimal));

    const Rect2D renderArea{.pos = {0.0f, 0.0f}, .extent = glm::vec2(facts.viewExtent.width, facts.viewExtent.height)};

    (void)graph.addPass(
        makeViewGraphName(deferred_pass_names::kSSAO, ctx.viewId),
        [inputs, noise, output, renderArea](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(inputs.frame, inputs.frameRange);
            passBuilder.read(inputs.albedo);
            passBuilder.read(inputs.normal);
            passBuilder.read(inputs.depth);
            passBuilder.read(noise);
            passBuilder.declareRaster({
                .renderArea = renderArea,
                .layerCount = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(1.0f, 1.0f, 1.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [stage, prepared, inputs, noise](RGRenderContext& rgCtx) {
            stage->writeInputDescriptors(
                prepared.inputDS,
                rgCtx.getBindingContext(),
                inputs.albedo,
                inputs.normal,
                inputs.depth,
                noise);

            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent = rasterParams.getRenderExtent();
            const auto viewWidth    = renderExtent.width;
            const auto viewHeight   = renderExtent.height;
            rgCtx.beginDeclaredRasterRendering();

            rgCtx.getCommandBuffer().bindPipeline(stage->getPipeline());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(viewWidth), static_cast<float>(viewHeight));
            rgCtx.getCommandBuffer().setScissor(0, 0, viewWidth, viewHeight);
            rgCtx.getCommandBuffer().bindDescriptorSets(
                stage->getPipelineLayout(), 0, {prepared.frameDS, prepared.inputDS});
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });

    outputs.ao = OptionalAO{output};
    return outputs;
}

} // namespace ya
