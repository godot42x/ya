#include "Render3D/Deferred/Features/BloomFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"

namespace ya
{

BloomFeature::Resolved BloomFeature::resolve(const Settings& settings,
                                             const Facts&    facts,
                                             const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    if (!resolved.bEnabled) {
        return resolved;
    }

    resolved.extract = ViewTargetRequirement{
        .role   = EViewAttachment::BloomExtract,
        .format = BloomPostprocessing::BLOOM_FORMAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    resolved.blur = ViewTargetRequirement{
        .role   = EViewAttachment::BloomBlur,
        .format = BloomPostprocessing::BLOOM_FORMAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    resolved.composite = ViewTargetRequirement{
        .role   = EViewAttachment::BloomComposite,
        .format = BloomPostprocessing::BLOOM_FORMAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    return resolved;
}

Result<BloomFeature::Prepared, FeatureError> BloomFeature::prepareBindings(const Resolved&       resolved,
                                                                           const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    if (ctx.submission == nullptr || ctx.stage == nullptr) {
        return FeatureError::MissingContext;
    }

    allocateBloomPassBindings(*ctx.submission,
                              ctx.stage->getBloomExtractDSL(),
                              ctx.stage->getBloomBlurDSL(),
                              ctx.stage->getBloomCompositeDSL(),
                              prepared.bloom);
    return prepared;
}

Result<BloomFeature::GraphOutputs, FeatureError> BloomFeature::append(RenderGraph&               graph,
                                                                      PostProcessingStage*       stage,
                                                                      const FeatureGraphContext& ctx,
                                                                      const Facts&               facts,
                                                                      const Resolved&            resolved,
                                                                      const Prepared&            prepared,
                                                                      const Inputs&              inputs) const
{
    GraphOutputs outputs;
    outputs.color = inputs.color;
    if (!resolved.bEnabled) {
        return outputs;
    }
    if (stage == nullptr || ctx.viewLease == nullptr || !inputs.color.isValid()) {
        return FeatureError::MissingContext;
    }

    const auto importTarget = [&](EViewAttachment role, std::string_view label, const std::optional<ViewTargetRequirement>& requirement) {
        if (!requirement.has_value()) {
            return RGTextureHandle{};
        }
        const std::shared_ptr<RenderTexture> target = ctx.viewLease->find(role);
        if (target == nullptr) {
            return RGTextureHandle{};
        }
        return graph.importTexture(
            makeImportedTextureDesc(*target, label, EImageLayout::ShaderReadOnlyOptimal, requirement->usage));
    };

    const RGTextureHandle extract = importTarget(EViewAttachment::BloomExtract, "Deferred.BloomExtract", resolved.extract);
    const RGTextureHandle blur    = importTarget(EViewAttachment::BloomBlur, "Deferred.BloomBlur", resolved.blur);
    const RGTextureHandle composite = importTarget(EViewAttachment::BloomComposite, "Deferred.BloomComposite", resolved.composite);
    if (!extract.isValid() || !blur.isValid() || !composite.isValid()) {
        YA_CORE_ERROR("Bloom is enabled but the View has no prepared Bloom targets");
        return FeatureError::MissingDependency;
    }

    const RGTextureHandle result = stage->appendBloomGraphPasses(
        graph, inputs.color, facts.viewExtent, nullptr, ctx.viewId, prepared.bloom, extract, blur, composite);
    if (!result.isValid()) {
        return FeatureError::MissingDependency;
    }
    outputs.color = result;
    return outputs;
}

} // namespace ya
