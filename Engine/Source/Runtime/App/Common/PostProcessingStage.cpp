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

} // namespace

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

    _bloomCompositeTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
        .label   = "BloomCompositeRT",
        .width   = extent.width,
        .height  = extent.height,
        .format  = EFormat::R16G16B16A16_SFLOAT,
        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .samples = ESampleCount::Sample_1,
        .isDepth = false,
    });
}

void PostProcessingStage::recreateBloomTextures(Extent2D extent)
{
    if (!_render || extent.width == 0 || extent.height == 0) {
        return;
    }

    const RenderTextureCreateInfo ci{
        .label = "BloomRT",
        .width = extent.width,
        .height = extent.height,
        .format = EFormat::R16G16B16A16_SFLOAT,
        .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .samples = ESampleCount::Sample_1,
        .isDepth = false,
    };

    auto extractCI = ci;
    extractCI.label = "BloomExtractRT";
    _bloomExtractTexture = Texture::createRenderTexture(extractCI);

    auto pingCI = ci;
    pingCI.label = "BloomBlurPingRT";
    _bloomBlurPingTexture = Texture::createRenderTexture(pingCI);

    auto pongCI = ci;
    pongCI.label = "BloomBlurPongRT";
    _bloomBlurPongTexture = Texture::createRenderTexture(pongCI);
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

    _bloomExtractTexture.reset();
    _bloomBlurPingTexture.reset();
    _bloomBlurPongTexture.reset();
    _bloomCompositeTexture.reset();
    _postprocessTexture.reset();
    _render = nullptr;
}

void PostProcessingStage::beginFrame()
{
    if (_bloomProcessor) {
        _bloomProcessor->beginFrame();
    }
    if (_postProcessor) {
        _postProcessor->beginFrame();
    }
}

void PostProcessingStage::renderGUI()
{
    if (!ImGui::TreeNode("PostProcessingStage")) {
        return;
    }

    if (ImGui::TreeNode("Settings")) {
        if (ImGui::Checkbox("Enabled", &bEnabled)) {
            ConfigManager::Editor(POSTPROCESS_CONFIG_DOC_NAME)
                .set(POSTPROCESS_CONFIG_KEY_ENABLE, bEnabled);
        }
        ImGui::TreePop();
    }

    if (_postProcessor && ImGui::TreeNode("Processor")) {
        if (_bloomProcessor) {
            _bloomProcessor->renderGUI(_state);
        }
        _postProcessor->renderGUI(_state);
        ImGui::TreePop();
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
        recreateBloomTextures(inputExtent);
    }
    if (!_postprocessTexture) {
        return inputTexture;
    }

    Texture* compositeInput = inputTexture;
    if (_state.bEnableBloom && _bloomProcessor && _bloomExtractTexture && _bloomBlurPingTexture && _bloomBlurPongTexture) {
        YA_CORE_ASSERT(_bloomCompositeTexture, "Bloom composite texture should be created with postprocess output");
        _bloomProcessor->render(BloomPostprocessing::RenderDesc{
            .cmdBuf = cmdBuf,
            .sceneTexture = inputTexture,
            .outputTexture = _bloomCompositeTexture.get(),
            .bloomExtract = _bloomExtractTexture.get(),
            .blurPingTexture = _bloomBlurPingTexture.get(),
            .blurPongTexture = _bloomBlurPongTexture.get(),
            .renderExtent = inputExtent,
            .state = &_state,
        });
        compositeInput = _bloomCompositeTexture.get();
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
        .inputImageView = compositeInput->getImageView(),
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
    recreateBloomTextures(newExtent);
}

} // namespace ya
