#include "Runtime/App/Common/PostProcessingStage.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Swapchain.h"
#include "imgui.h"

namespace ya
{

void PostProcessingStage::recreateOutputTexture(Extent2D extent)
{
    if (!_render || extent.width == 0 || extent.height == 0) {
        return;
    }

    _postprocessTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
        .label   = "PostprocessRT",
        .width   = extent.width,
        .height  = extent.height,
        .format  = _colorFormat,
        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .samples = ESampleCount::Sample_1,
        .isDepth = false,
    });
}

void PostProcessingStage::init(const InitDesc& desc)
{
    _render      = desc.render;
    _colorFormat = desc.colorFormat;
    recreateOutputTexture(Extent2D{.width = desc.width, .height = desc.height});

    _postProcessor = ya::makeShared<BasicPostprocessing>();
    _postProcessor->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "BasicPostprocessing",
            .viewMask                = 0,
            .colorAttachmentFormats  = {desc.colorFormat},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
}

void PostProcessingStage::shutdown()
{
    if (_postProcessor) {
        _postProcessor->shutdown();
        _postProcessor.reset();
    }

    _postprocessTexture.reset();
    _render = nullptr;
}

void PostProcessingStage::beginFrame()
{
    if (_postProcessor) {
        _postProcessor->beginFrame();
    }
}

void PostProcessingStage::renderGUI()
{
    if (!ImGui::TreeNode("PostProcessingStage")) {
        return;
    }

    ImGui::Checkbox("Enabled", &bEnabled);
    if (_postProcessor) {
        _postProcessor->renderGUI(_state);
    }

    ImGui::TreePop();
}

Texture* PostProcessingStage::execute(ICommandBuffer* cmdBuf,
                                      Texture*        inputTexture,
                                      glm::vec2       viewportExtent,
                                      FrameContext*   ctx)
{
    (void)viewportExtent;

    if (!bEnabled || !_postProcessor) {
        return inputTexture;
    }
    if (!cmdBuf || !inputTexture) {
        return inputTexture;
    }

    const Extent2D inputExtent = inputTexture->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return inputTexture;
    }

    if (!_postprocessTexture || _postprocessTexture->getExtent() != inputExtent) {
        _render->waitIdle();
        recreateOutputTexture(inputExtent);
    }
    if (!_postprocessTexture) {
        return inputTexture;
    }

    cmdBuf->debugBeginLabel("Postprocessing");
    cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(), EImageLayout::ColorAttachmentOptimal);

    RenderingInfo ri{
        .label      = "Postprocessing",
        .renderArea = Rect2D{
            .pos    = {0, 0},
            .extent = inputExtent.toVec2(),
        },
        .layerCount       = 1,
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        .depthClearValue  = ClearValue(1.0f, 0),
        .colorAttachments = {
            RenderingInfo::ImageSpec{
                .texture = _postprocessTexture.get(),
                .loadOp  = EAttachmentLoadOp::Clear,
                .storeOp = EAttachmentStoreOp::Store,
            },
        },
    };

    cmdBuf->beginRendering(ri);

    const auto swapchainFormat = _render->getSwapchain()->getFormat();
    const bool bOutputIsSRGB   = EFormat::isSRGB(swapchainFormat);
    _postProcessor->render(BasicPostprocessing::RenderDesc{
        .cmdBuf         = cmdBuf,
        .ctx            = ctx,
        .inputImageView = inputTexture->getImageView(),
        .renderExtent   = inputExtent,
        .bOutputIsSRGB  = bOutputIsSRGB,
        .state          = &_state,
    });

    cmdBuf->endRendering(ri);
    cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(), EImageLayout::ShaderReadOnlyOptimal);
    cmdBuf->debugEndLabel();
    return _postprocessTexture.get();
}

void PostProcessingStage::onViewportResized(Extent2D newExtent)
{
    if (!_render || !_postprocessTexture || newExtent.width == 0 || newExtent.height == 0) {
        return;
    }

    if (_postprocessTexture->getExtent() == newExtent) {
        return;
    }

    _render->waitIdle();
    recreateOutputTexture(newExtent);
}

} // namespace ya