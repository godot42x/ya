#include "Render3D/Composition/Features/BloomFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderSubmission.h"
#include "RHI/Render.h"

#include <string>

namespace ya
{

void BloomFeature::init(const FeatureInitContext& ctx)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<BloomPostprocessing>();
    }
    _stage->init(BloomPostprocessing::InitDesc{
        .render = ctx.render,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "BloomPostprocessing",
            .viewMask                = 0,
            .colorAttachmentFormats  = {BloomPostprocessing::BLOOM_FORMAT},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
}

void BloomFeature::shutdown()
{
    if (_stage) {
        _stage->shutdown();
        _stage.reset();
    }
}

void BloomFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

BloomFeature::Resolved BloomFeature::resolve(const Settings& settings,
                                             const Facts&    facts,
                                             const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled          = settings.bEnabled;
    resolved.importLabelPrefix = settings.importLabelPrefix;
    resolved.state             = settings.state;
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
                              ctx.stage->getExtractDSL(),
                              ctx.stage->getBlurDSL(),
                              ctx.stage->getCompositeDSL(),
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

    // The chain runs with the resolved state; grading is not this feature's
    // decision (its bEnabled already folds in the master switch).
    PostProcessingState chainState = resolved.state;
    chainState.bEnableBloom        = true;
    const BloomPostprocessing::ChainOutputs chain = stage->appendBloomChain(
        graph,
        BloomPostprocessing::RenderDesc{
            .sceneHandle  = inputs.color,
            .renderExtent = facts.viewExtent,
            .state        = &chainState,
            .viewId       = ctx.viewId,
            .bloom        = prepared.bloom,
            .extractHandle   = outputs.extract,
            .blurHandle      = outputs.blur,
            .compositeHandle = outputs.composite,
        });
    if (!chain.composite.isValid()) {
        return FeatureError::MissingDependency;
    }
    outputs.color = chain.composite;
    return outputs;
}

} // namespace ya
