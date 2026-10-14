#include "Render3D/Composition/Features/FinalizeFeature.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewGraphName.h"
#include "RHI/Core/Swapchain.h"

namespace ya
{

namespace
{
/// Diagnostic export name of the finalize output. The pipeline reads its own
/// typed handle back for publication; this name exists for the topology dump.
constexpr std::string_view kFinalizeOutputExportName = "Postprocessing.Output";
} // namespace

void FinalizeFeature::init(const FeatureInitContext& ctx)
{
    if (_stage == nullptr) {
        _stage = ya::makeShared<BasicPostprocessing>();
    }
    _stage->init(BasicPostprocessing::InitDesc{
        .render                = ctx.render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "BasicPostprocessing",
            .viewMask                = 0,
            .colorAttachmentFormats  = {ctx.postProcessColorFormat},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
}

void FinalizeFeature::shutdown()
{
    if (_stage) {
        _stage->shutdown();
        _stage.reset();
    }
}

void FinalizeFeature::beginFrame()
{
    if (_stage) {
        _stage->beginFrame();
    }
}

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
    // Fold the master switch here so append records one decided value.
    resolved.grading = settings.bGradingEnabled ? settings.grading : settings.grading.withoutGrading();
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
    prepared.toneMap.input.set = allocateCombinedImageSamplerSet(*ctx.submission, ctx.stage->getInputDSL(), 1);
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

    if (facts.viewExtent.width == 0 || facts.viewExtent.height == 0 || !inputs.color.isValid()) {
        return FeatureError::MissingDependency;
    }

    // The state the pass runs with: the configured grading when the master
    // switch is on, and "no look changes, keep the display encoding" when it is
    // off. The pass itself always runs -- it is what makes a display image.
    const PostProcessingState& grading = resolved.grading;

    [[maybe_unused]] const RGPassHandle pass = graph.addPass(
        makeViewGraphName("Postprocessing", ctx.viewId),
        [input = inputs.color, output = display, inputExtent = facts.viewExtent](RGPassBuilder& pass) {
            pass.read(input);
            pass.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = inputExtent.toVec2()},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        // `stage` (the pipeline-owned device object), not `this`: this feature
        // is a stack temporary in the recipe, and this lambda outlives that
        // frame. Every other feature captures the passed-in stage pointer the
        // same way.
        [stage, input = inputs.color, inputExtent = facts.viewExtent, bOutputIsSRGB = resolved.bOutputIsSRGB, grading, postContext = facts.postContext, viewId = ctx.viewId, toneMap = prepared.toneMap](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            const auto* compositeInputImage = rgCtx.resolveTexture(input);
            YA_CORE_ASSERT(compositeInputImage != nullptr && compositeInputImage->getImageView() != nullptr,
                           "Finalize failed to resolve input texture {}", input.index);
            stage->render(BasicPostprocessing::RenderDesc{
                .cmdBuf         = &rgCtx.getCommandBuffer(),
                .ctx            = postContext,
                .inputImageView = compositeInputImage->getImageView(),
                .renderExtent   = inputExtent,
                .bOutputIsSRGB  = bOutputIsSRGB,
                .state          = &grading,
                .viewId         = viewId,
                .toneMap        = toneMap,
            });

            rgCtx.endRendering();
        });

    graph.exportTexture(display, makeViewGraphName(kFinalizeOutputExportName, ctx.viewId));
    outputs.display = display;
    return outputs;
}

} // namespace ya
