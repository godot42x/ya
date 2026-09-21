#include "Render3D/Pipelines/SurfaceWritePass.h"

#include "Core/Log.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Render.h"

namespace ya
{

namespace
{

/// The surface takes the image exactly as the View's finalize left it. The
/// pass-through state is not a setting a caller can change: this pass has no
/// opinion about how an image looks, which is what keeps "the window is graded
/// twice" from being reachable again.
const PostProcessingState kSurfaceWriteState = PostProcessingState::passThrough();

} // namespace

void SurfaceWritePass::init(const InitDesc& desc)
{
    _render        = desc.render;
    _surfaceFormat = desc.surfaceFormat;

    auto writer = ya::makeShared<BasicPostprocessing>();
    writer->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "SurfaceWrite",
            .viewMask                = 0,
            .colorAttachmentFormats  = {_surfaceFormat},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
    _writer = std::move(writer);
}

void SurfaceWritePass::shutdown()
{
    if (_writer) {
        _writer->shutdown();
        _writer.reset();
    }
    _render        = nullptr;
    _surfaceFormat = EFormat::Undefined;
}

void SurfaceWritePass::writeSurfaceBackdrop(ICommandBuffer&   cmdBuf,
                                            RenderSubmission& submission,
                                            IImageView&       image,
                                            Extent2D          extent)
{
    if (!_writer) {
        return;
    }

    auto inputSet =
        allocateCombinedImageSamplerSet(submission, _writer->getInputDSL(), /*descriptorsPerSet=*/1);
    if (!inputSet) {
        YA_CORE_ERROR("SurfaceWritePass could not allocate its input descriptor set for this flight");
        return;
    }

    _writer->beginFrame();
    _writer->render(BasicPostprocessing::RenderDesc{
        .cmdBuf         = &cmdBuf,
        .ctx            = nullptr,
        .inputImageView = &image,
        .renderExtent   = extent,
        .bOutputIsSRGB  = EFormat::isSRGB(_surfaceFormat),
        .state          = &kSurfaceWriteState,
        .toneMap        = ToneMapPassBindings{.input = {.set = inputSet}},
    });
}

} // namespace ya
