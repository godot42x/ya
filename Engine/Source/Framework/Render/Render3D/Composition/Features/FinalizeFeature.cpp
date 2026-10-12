#include "Render3D/Composition/Features/FinalizeFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderSubmission.h"

namespace ya
{

FinalizeFeature::Resolved FinalizeFeature::resolve(const Settings& settings,
                                                   const Facts&    facts,
                                                   const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled      = settings.bEnabled;
    resolved.bOutputIsSRGB = settings.bOutputIsSRGB;
    resolved.importLabel   = settings.importLabel;
    resolved.target = ViewTargetRequirement{
        .role   = EViewAttachment::DisplayColor,
        .format = settings.displayFormat,
        .usage  = settings.displayUsage,
    };
    return resolved;
}

Result<FinalizeFeature::Prepared, FeatureError> FinalizeFeature::prepareBindings(const Resolved&       resolved,
                                                                                 const PrepareContext& ctx) const
{
    // Finalize is mandatory: a composition that resolves it off is a wiring bug,
    // not a legal skipped feature.
    if (!resolved.bEnabled) {
        return FeatureError::InvalidInput;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    Prepared prepared;
    prepared.toneMap.input.set = allocateCombinedImageSamplerSet(*ctx.submission, ctx.stage->getToneMapInputDSL(), 1);
    if (!prepared.toneMap.input.set) {
        return FeatureError::AllocationFailed;
    }
    return prepared;
}

Result<FinalizeFeature::GraphOutputs, FeatureError> FinalizeFeature::append(RenderGraph&               graph,
                                                                            Stage*                     stage,
                                                                            const FeatureGraphContext& ctx,
                                                                            const Facts&               facts,
                                                                            const Resolved&            resolved,
                                                                            const Prepared&            prepared,
                                                                            const Inputs&              inputs) const
{
    GraphOutputs outputs;
    if (!resolved.bEnabled) {
        return FeatureError::InvalidInput;
    }
    if (stage == nullptr || ctx.viewLease == nullptr) {
        return FeatureError::MissingContext;
    }

    const std::shared_ptr<RenderTexture> target = ctx.viewLease->find(resolved.target.role);
    if (target == nullptr) {
        return FeatureError::MissingDependency;
    }
    const RGTextureHandle display = graph.importTexture(makeImportedTextureDesc(
        *target, resolved.importLabel, EImageLayout::ShaderReadOnlyOptimal, resolved.target.usage));

    const RGTextureHandle result = stage->appendFinalizeGraphPasses(
        graph,
        PostProcessingStage::FinalizePassParams{
            .input         = inputs.color,
            .output        = display,
            .inputExtent   = facts.viewExtent,
            .bOutputIsSRGB = resolved.bOutputIsSRGB,
            .postContext   = facts.postContext,
            .viewId        = ctx.viewId,
            .toneMap       = prepared.toneMap,
        });
    if (!result.isValid()) {
        return FeatureError::MissingDependency;
    }
    outputs.display = result;
    return outputs;
}

} // namespace ya
