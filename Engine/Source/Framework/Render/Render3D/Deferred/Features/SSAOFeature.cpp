#include "Render3D/Deferred/Features/SSAOFeature.h"

#include "Core/Log.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Deferred/DeferredFrameGraphPasses.h"
#include "RHI/Backend/TextureLibrary.h"

namespace ya
{

SSAOFeature::Resolved SSAOFeature::resolve(const Settings& settings,
                                           const Facts& facts,
                                           const Inputs& inputs) const
{
    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    resolved.settings = settings;
    resolved.facts    = facts;
    resolved.inputs   = inputs;

    // Only declare resource requirements when enabled
    if (settings.bEnabled) {
        resolved.aoTarget = Resolved::ResourceRequirement{
            .role   = EViewAttachment::SSAO,
            .format = SSAOStage::AO_FORMAT,
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
    }

    return resolved;
}

Result<SSAOFeature::Prepared, FeatureError> SSAOFeature::prepareBindings(const Resolved& resolved,
                                                           const PreparedInputs& inputs,
                                                           const PrepareContext& ctx)
{
    Prepared prepared;
    prepared.bEnabled = resolved.bEnabled;

    // When disabled, return minimal prepared state (no UBO, no DS)
    if (!resolved.bEnabled) {
        return prepared;
    }

    if (!stage) {
        YA_CORE_ERROR("SSAOFeature::prepareBindings called without stage");
        return FeatureError::MissingDependency;
    }

    if (!ctx.viewTargets || !ctx.submission) {
        YA_CORE_ERROR("SSAOFeature::prepareBindings missing ViewTargetStore or RenderSubmission");
        return FeatureError::MissingContext;
    }

    // Build frame data (UBO content)
    RenderStageContext stageCtx;
    stageCtx.viewExtent = resolved.facts.viewExtent;
    stageCtx.frameData  = resolved.facts.frameData;
    prepared.frameData = stage->buildFrameData(stageCtx);

    // C1: Frame UBO and descriptor sets are managed by existing DeferredFrameResourceSet
    // Full implementation in C2-C3 will allocate them here via RenderSubmission

    return prepared;
}

Result<SSAOFeature::GraphOutputs, FeatureError> SSAOFeature::append(const Resolved& resolved,
                                                      const Prepared& prepared,
                                                      const PreparedInputs& inputs,
                                                      const FeatureGraphContext& ctx)
{
    GraphOutputs outputs;

    // When disabled, return invalid OptionalAO
    if (!resolved.bEnabled || !prepared.bEnabled) {
        outputs.ao = OptionalAO{}; // invalid
        return outputs;
    }

    if (!stage || !ctx.graph || !ctx.stageCtx) {
        YA_CORE_ERROR("SSAOFeature::append missing stage or graph context");
        return FeatureError::MissingContext;
    }

    // Import AO output texture from ViewTargetStore
    if (!ctx.viewTargets) {
        YA_CORE_ERROR("SSAOFeature::append missing ViewTargetStore in context");
        return FeatureError::MissingContext;
    }

    ViewTargetLease aoLease = ctx.viewTargets->lease(resolved.facts.viewId);
    std::shared_ptr<RenderTexture> aoTexture = aoLease.find(EViewAttachment::SSAO);
    
    if (!aoTexture) {
        YA_CORE_ERROR("SSAOFeature::append failed to find SSAO attachment for view {}", resolved.facts.viewId);
        return FeatureError::AllocationFailed;
    }

    RGTextureHandle aoOutput = ctx.graph->importTexture(makeImportedTextureDesc(
        aoTexture->getResourceShared(),
        makeViewGraphName("SSAO.Output", resolved.facts.viewId),
        EImageLayout::Undefined));

    // Build DeferredSSAOPassParams using prepared descriptor sets and graph handles
    DeferredSSAOPassParams params;
    params.viewId             = resolved.facts.viewId;
    params.albedo             = inputs.albedo;
    params.normal             = inputs.normal;
    params.depth              = inputs.depth;
    params.frame              = inputs.frameBuffer;
    params.frameRange         = {prepared.frameUBOOffset, prepared.frameUBOSize};
    params.frameDescriptorSet = prepared.frameDS;
    params.inputDescriptorSet = prepared.inputDS;
    params.output             = aoOutput;

    // Append graph pass via existing SSAOStage::appendGraphPass
    RGTextureHandle aoResult = stage->appendGraphPass(*ctx.graph, *ctx.stageCtx, params, aoOutput);

    // Return valid OptionalAO
    outputs.ao = OptionalAO{aoResult};
    return outputs;
}

} // namespace ya
