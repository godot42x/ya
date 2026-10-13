#include "Render3D/Composition/Features/BloomFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"

#include <string>

namespace ya
{

BloomFeature::Resolved BloomFeature::resolve(const Settings& settings,
                                             const Facts&    facts,
                                             const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled          = settings.bEnabled;
    resolved.importLabelPrefix = settings.importLabelPrefix;
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
                                                                      Stage*                     stage,
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

    const auto importTarget = [&](EViewAttachment role, std::string_view label, const std::optional<ViewTargetRequirement>& requirement) -> RGTextureHandle {
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

    const std::string labelPrefix{resolved.importLabelPrefix};
    outputs.extract = importTarget(
        EViewAttachment::BloomExtract, labelPrefix + "BloomExtract", resolved.extract);
    outputs.blur = importTarget(
        EViewAttachment::BloomBlur, labelPrefix + "BloomBlur", resolved.blur);
    outputs.composite = importTarget(
        EViewAttachment::BloomComposite, labelPrefix + "BloomComposite", resolved.composite);
    if (!outputs.extract.isValid() || !outputs.blur.isValid() || !outputs.composite.isValid()) {
        YA_CORE_ERROR("Bloom is enabled but the View has no prepared Bloom targets");
        return FeatureError::MissingDependency;
    }

    const BloomPostprocessing::ChainOutputs chain = stage->appendBloomGraphPasses(
        graph, inputs.color, facts.viewExtent, nullptr, ctx.viewId, prepared.bloom, outputs.extract, outputs.blur, outputs.composite);
    if (!chain.composite.isValid()) {
        return FeatureError::MissingDependency;
    }
    outputs.color = chain.composite;
    return outputs;
}

} // namespace ya
