#include "Runtime/App/Common/PostProcessingStage.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Swapchain.h"
#include "imgui.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr const char* POSTPROCESS_CONFIG_DOC_NAME                    = "editor";
constexpr const char* POSTPROCESS_CONFIG_KEY_ENABLE                  = "render.postprocess.enabled";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_ENABLE            = "render.postprocess.bloom.enabled";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_THRESHOLD         = "render.postprocess.bloom.threshold";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_SOFT_KNEE         = "render.postprocess.bloom.softKnee";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_EXTRACT_INTENSITY = "render.postprocess.bloom.extractIntensity";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_BLUR_PASSES       = "render.postprocess.bloom.blurPasses";
constexpr const char* POSTPROCESS_CONFIG_KEY_BLOOM_STRENGTH          = "render.postprocess.bloom.strength";
constexpr const char* POSTPROCESS_CONFIG_KEY_INVERSION               = "render.postprocess.basic.inversion";
constexpr const char* POSTPROCESS_CONFIG_KEY_GRAYSCALE               = "render.postprocess.basic.grayscale";
constexpr const char* POSTPROCESS_CONFIG_KEY_KERNEL                  = "render.postprocess.basic.kernel";
constexpr const char* POSTPROCESS_CONFIG_KEY_KERNEL_TEXEL_OFFSET     = "render.postprocess.basic.kernelTexelOffset";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_ENABLE      = "render.postprocess.basic.tonemapping.enabled";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_CURVE       = "render.postprocess.basic.tonemapping.curve";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_EXPOSURE    = "render.postprocess.basic.tonemapping.exposure";
constexpr const char* POSTPROCESS_CONFIG_KEY_GAMMA_CORRECTION_ENABLE = "render.postprocess.basic.output.gammaCorrection";
constexpr const char* POSTPROCESS_CONFIG_KEY_GAMMA                   = "render.postprocess.basic.output.gamma";
constexpr const char* POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_ENABLE     = "render.postprocess.basic.output.randomGrain";
constexpr const char* POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_STRENGTH   = "render.postprocess.basic.output.randomGrainStrength";

stdptr<RenderImage> createPostprocessRenderImage(IRender* render,
                                                 std::string_view label,
                                                 Extent2D extent,
                                                 EFormat::T format)
{
    if (!render || extent.width == 0 || extent.height == 0) {
        return nullptr;
    }

    return createRenderImage(
        *render->getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = std::string(label),
                .format        = format,
                .extent        = {.width = extent.width, .height = extent.height, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = std::string(label) + "_DefaultView",
                .aspectFlags = EImageAspect::Color,
            },
        });
}

} // namespace

void PostProcessingStage::requestResize(Extent2D extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    _pendingResizeExtent = extent;
    _bResizePending      = true;
}

void PostProcessingStage::applyPendingResize()
{
    if (!_bResizePending || !_render) {
        return;
    }

    _render->waitIdle();
    recreateOutputTexture(_pendingResizeExtent);
    recreateBloomTextures(_pendingResizeExtent);
    _bResizePending = false;
}

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
        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
        .samples = ESampleCount::Sample_1,
        .isDepth = false,
    });

    _bloomCompositeImage = createPostprocessRenderImage(_render, "BloomCompositeRT", extent, EFormat::R16G16B16A16_SFLOAT);
}

void PostProcessingStage::recreateBloomTextures(Extent2D extent)
{
    if (!_render || extent.width == 0 || extent.height == 0) {
        return;
    }

    _bloomExtractImage  = createPostprocessRenderImage(_render, "BloomExtractRT", extent, EFormat::R16G16B16A16_SFLOAT);
    _bloomBlurPingImage = createPostprocessRenderImage(_render, "BloomBlurPingRT", extent, EFormat::R16G16B16A16_SFLOAT);
    _bloomBlurPongImage = createPostprocessRenderImage(_render, "BloomBlurPongRT", extent, EFormat::R16G16B16A16_SFLOAT);
}

void PostProcessingStage::init(const InitDesc& desc)
{
    _render      = desc.render;
    _colorFormat = desc.colorFormat;

    auto& config               = ConfigManager::get();
    bEnabled                   = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_ENABLE, bEnabled);
    _state.bEnableBloom        = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_ENABLE, _state.bEnableBloom);
    _state.bloomThreshold      = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_THRESHOLD, _state.bloomThreshold);
    _state.bloomSoftKnee       = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_SOFT_KNEE, _state.bloomSoftKnee);
    _state.bloomExtractIntensity = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_EXTRACT_INTENSITY, _state.bloomExtractIntensity);
    _state.bloomBlurPasses     = static_cast<uint32_t>(std::max(1, config.getOr<int>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_BLUR_PASSES, static_cast<int>(_state.bloomBlurPasses))));
    _state.bloomStrength       = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_BLOOM_STRENGTH, _state.bloomStrength);
    _state.bEnableInversion    = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_INVERSION, _state.bEnableInversion);
    _state.grayscaleMode       = static_cast<PostProcessingState::EGrayscaleMode>(config.getOr<int>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_GRAYSCALE, static_cast<int>(_state.grayscaleMode)));
    _state.kernelMode          = static_cast<PostProcessingState::EKernelMode>(config.getOr<int>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_KERNEL, static_cast<int>(_state.kernelMode)));
    _state.kernelTexelOffset   = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_KERNEL_TEXEL_OFFSET, _state.kernelTexelOffset);
    _state.bEnableToneMapping  = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_TONEMAPPING_ENABLE, _state.bEnableToneMapping);
    _state.toneMappingCurve    = static_cast<PostProcessingState::EToneMappingCurve>(config.getOr<int>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_TONEMAPPING_CURVE, static_cast<int>(_state.toneMappingCurve)));
    _state.exposure            = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_TONEMAPPING_EXPOSURE, _state.exposure);
    _state.bEnableGammaCorrection = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_GAMMA_CORRECTION_ENABLE, _state.bEnableGammaCorrection);
    _state.gamma               = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_GAMMA, _state.gamma);
    _state.bEnableRandomGrain  = config.getOr<bool>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_ENABLE, _state.bEnableRandomGrain);
    _state.randomGrainStrength = config.getOr<float>(POSTPROCESS_CONFIG_DOC_NAME, POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_STRENGTH, _state.randomGrainStrength);

    recreateOutputTexture(Extent2D{.width = desc.width, .height = desc.height});
    recreateBloomTextures(Extent2D{.width = desc.width, .height = desc.height});

    _bloomProcessor = ya::makeShared<BloomPostprocessing>();
    _bloomProcessor->init(BloomPostprocessing::InitDesc{
        .render = _render,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label = "BloomPostprocessing",
            .viewMask = 0,
            .colorAttachmentFormats = {EFormat::R16G16B16A16_SFLOAT},
            .depthAttachmentFormat = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });

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
    if (_bloomProcessor) {
        _bloomProcessor->shutdown();
        _bloomProcessor.reset();
    }
    if (_postProcessor) {
        _postProcessor->shutdown();
        _postProcessor.reset();
    }

    _bloomExtractImage.reset();
    _bloomBlurPingImage.reset();
    _bloomBlurPongImage.reset();
    _bloomCompositeImage.reset();
    _postprocessTexture.reset();
    _pendingResizeExtent = {};
    _bResizePending      = false;
    _render = nullptr;
}

void PostProcessingStage::beginFrame()
{
    applyPendingResize();

    if (_bloomProcessor) {
        _bloomProcessor->beginFrame();
    }
    if (_postProcessor) {
        _postProcessor->beginFrame();
    }
}

void PostProcessingStage::renderSettingsGUI()
{
    if (ImGui::Checkbox("Enable Post Process", &bEnabled)) {
        ConfigManager::Editor(POSTPROCESS_CONFIG_DOC_NAME)
            .set(POSTPROCESS_CONFIG_KEY_ENABLE, bEnabled);
    }

    if (_bloomProcessor) {
        ImGui::SeparatorText("Bloom");
        _bloomProcessor->renderSettingsGUI(_state);
    }

    if (_postProcessor) {
        ImGui::SeparatorText("Image Effects");
        _postProcessor->renderSettingsGUI(_state);
    }
}

void PostProcessingStage::renderTechnicalGUI()
{
    if (_bloomProcessor) {
        _bloomProcessor->renderTechnicalGUI();
    }
    if (_postProcessor) {
        _postProcessor->renderTechnicalGUI();
    }
}

void PostProcessingStage::renderGUI()
{
    if (!ImGui::TreeNode("Post Process")) {
        return;
    }

    renderSettingsGUI();
    renderTechnicalGUI();

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
        requestResize(inputExtent);
        return inputTexture;
    }
    if (!_postprocessTexture) {
        return inputTexture;
    }

    IImageView* compositeInputView = inputTexture->getImageView();
    if (_state.bEnableBloom && _bloomProcessor && _bloomExtractImage && _bloomBlurPingImage && _bloomBlurPongImage) {
        YA_CORE_ASSERT(_bloomCompositeImage, "Bloom composite image should be created with postprocess output");
        _bloomProcessor->render(BloomPostprocessing::RenderDesc{
            .cmdBuf = cmdBuf,
            .sceneImageView = inputTexture->getImageView(),
            .outputImage = _bloomCompositeImage.get(),
            .bloomExtract = _bloomExtractImage.get(),
            .blurPingImage = _bloomBlurPingImage.get(),
            .blurPongImage = _bloomBlurPongImage.get(),
            .renderExtent = inputExtent,
            .state = &_state,
        });
        compositeInputView = _bloomCompositeImage->getImageView();
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
                .image     = _postprocessTexture ? _postprocessTexture->getImage() : nullptr,
                .imageView = _postprocessTexture ? _postprocessTexture->getImageView() : nullptr,
                .loadOp    = EAttachmentLoadOp::Clear,
                .storeOp   = EAttachmentStoreOp::Store,
            },
        },
    };

    cmdBuf->beginRendering(ri);

    const auto swapchainFormat = _render->getSwapchain()->getFormat();
    const bool bOutputIsSRGB   = EFormat::isSRGB(swapchainFormat);
    _postProcessor->render(BasicPostprocessing::RenderDesc{
        .cmdBuf         = cmdBuf,
        .ctx            = ctx,
        .inputImageView = compositeInputView,
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

    requestResize(newExtent);
}

} // namespace ya
