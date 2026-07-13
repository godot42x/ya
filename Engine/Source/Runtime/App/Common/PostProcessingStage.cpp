#include "Runtime/App/Common/PostProcessingStage.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderGraphExecutor.h"
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

RGImportedTextureDesc makePostprocessImportedTextureDesc(const Texture& texture,
                                                         std::string_view label,
                                                         EImageLayout::T finalLayout)
{
    YA_CORE_ASSERT(texture.getImageShared() != nullptr, "Render graph import requires a backing image");

    IImage* image = texture.getImage();
    YA_CORE_ASSERT(image != nullptr, "Render graph import requires a valid image");

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = texture.getFormat(),
            .extent      = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels   = image->getMipLevels(),
            .arrayLayers = image->getArrayLayers(),
            .usage       = image->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = texture.getFormat(),
            .usage         = image->getUsage(),
            .extent        = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels     = image->getMipLevels(),
            .arrayLayers   = image->getArrayLayers(),
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = texture.getImageShared(),
    };
}

RGImportedTextureDesc makePostprocessImportedTextureDesc(const RenderImage& image,
                                                         std::string_view   label,
                                                         EImageLayout::T    finalLayout)
{
    YA_CORE_ASSERT(image.getImageShared() != nullptr, "Render graph import requires a backing image");

    IImage* rawImage = image.getImage();
    YA_CORE_ASSERT(rawImage != nullptr, "Render graph import requires a valid image");

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = image.getFormat(),
            .extent      = Extent3D{image.getWidth(), image.getHeight(), 1},
            .mipLevels   = rawImage->getMipLevels(),
            .arrayLayers = rawImage->getArrayLayers(),
            .usage       = rawImage->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(rawImage->getHandle()),
            .format        = image.getFormat(),
            .usage         = rawImage->getUsage(),
            .extent        = Extent3D{image.getWidth(), image.getHeight(), 1},
            .mipLevels     = rawImage->getMipLevels(),
            .arrayLayers   = rawImage->getArrayLayers(),
            .initialLayout = rawImage->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = image.getImageShared(),
        .viewDesc = ImageViewCreateInfo{
            .label       = std::string(label) + "_ImportedView",
            .aspectFlags = EImageAspect::Color,
        },
    };
}

} // namespace

void PostProcessingStage::refreshOutputTextureCompat()
{
    if (!_postprocessOutputImage || !_postprocessOutputImage->isValid()) {
        _postprocessOutputTextureCompat.reset();
        return;
    }

    const bool bNeedsWrapRefresh =
        !_postprocessOutputTextureCompat ||
        _postprocessOutputTextureCompat->getImage() != _postprocessOutputImage->getImage() ||
        _postprocessOutputTextureCompat->getImageView() != _postprocessOutputImage->getImageView();

    if (bNeedsWrapRefresh) {
        _postprocessOutputTextureCompat = Texture::wrap(
            _postprocessOutputImage->getImageShared(),
            _postprocessOutputImage->getImageViewShared(),
            "PostprocessRT_Compat");
    }
}

void PostProcessingStage::init(const InitDesc& desc)
{
    _render      = desc.render;
    _colorFormat = desc.colorFormat;
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

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

    _postprocessOutputImage = nullptr;
    _postprocessOutputTextureCompat.reset();

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
    _graphExecutor.reset();
    if (_bloomProcessor) {
        _bloomProcessor->shutdown();
        _bloomProcessor.reset();
    }
    if (_postProcessor) {
        _postProcessor->shutdown();
        _postProcessor.reset();
    }

    clearPreparedResources();
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

void PostProcessingStage::clearPreparedResources()
{
    _preparedGraphResources       = {};
    _postprocessOutputImage       = nullptr;
    _postprocessOutputTextureCompat.reset();
    if (_bloomProcessor) {
        _bloomProcessor->clearPreparedResources();
    }
}

void PostProcessingStage::resolvePreparedResources(const RenderGraphResourceRegistry& registry)
{
    if (_bloomProcessor) {
        _bloomProcessor->resolvePreparedResources(registry);
    }

    _postprocessOutputImage = _preparedGraphResources.output.isValid()
        ? registry.resolveTexture(_preparedGraphResources.output)
        : nullptr;
    refreshOutputTextureCompat();
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

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       Texture*     inputTexture,
                                                       glm::vec2    viewportExtent,
                                                       FrameContext* ctx)
{
    (void)viewportExtent;

    if (!bEnabled || !_postProcessor) {
        clearPreparedResources();
        return {};
    }
    if (!inputTexture) {
        clearPreparedResources();
        return {};
    }

    const Extent2D inputExtent = inputTexture->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        clearPreparedResources();
        return {};
    }

    clearPreparedResources();

    const auto input = graph.importTexture(makePostprocessImportedTextureDesc(*inputTexture, "Postprocessing.Input", EImageLayout::ShaderReadOnlyOptimal));
    RGTextureHandle compositeInput = input;
    if (_state.bEnableBloom && _bloomProcessor) {
        const auto bloomOutput = _bloomProcessor->appendGraphPasses(graph, BloomPostprocessing::RenderDesc{
            .sceneTexture = inputTexture,
            .sceneImageView = inputTexture->getImageView(),
            .renderExtent = inputExtent,
            .state = &_state,
        });
        if (bloomOutput.isValid()) {
            compositeInput = bloomOutput;
        }
    }

    const auto swapchainFormat = _render->getSwapchain()->getFormat();
    const bool bOutputIsSRGB   = EFormat::isSRGB(swapchainFormat);
    const auto  output = graph.createTexture(RGTextureDesc{
         .label  = "Postprocessing.Output",
         .format = _colorFormat,
         .extent = Extent3D{inputExtent.width, inputExtent.height, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);

    [[maybe_unused]] const auto pass = graph.addPass(
        "Postprocessing",
        [&](RGPassBuilder& pass) {
            pass.read(compositeInput);
            pass.useColorAttachment(output);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginColorRendering({
                .color      = output,
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = inputExtent.toVec2(),
                },
                .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            });

            const auto* compositeInputImage = rgCtx.resolveTexture(compositeInput);
            YA_CORE_ASSERT(compositeInputImage != nullptr && compositeInputImage->getImageView() != nullptr,
                           "Postprocessing failed to resolve input texture {}", compositeInput.index);
            _postProcessor->render(BasicPostprocessing::RenderDesc{
                .cmdBuf         = &rgCtx.getCommandBuffer(),
                .ctx            = ctx,
                .inputImageView = compositeInputImage->getImageView(),
                .renderExtent   = inputExtent,
                .bOutputIsSRGB  = bOutputIsSRGB,
                .state          = &_state,
            });

            rgCtx.endRendering();
        });

    _preparedGraphResources.input  = compositeInput;
    _preparedGraphResources.output = output;
    return output;
}

Texture* PostProcessingStage::execute(ICommandBuffer* cmdBuf,
                                      Texture*        inputTexture,
                                      glm::vec2       viewportExtent,
                                      FrameContext*   ctx)
{
    if (!cmdBuf || !inputTexture) {
        return inputTexture;
    }

    ICommandBuffer::LabelScope labelScope(cmdBuf, "Postprocessing");
    RenderGraph graph;
    const auto  output = appendGraphPasses(graph, inputTexture, viewportExtent, ctx);
    if (!output.isValid()) {
        return inputTexture;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "PostProcessingStage graph executor is not initialized");
    if (!_graphExecutor->execute(graph, *cmdBuf)) {
        clearPreparedResources();
        return inputTexture;
    }

    resolvePreparedResources(_graphExecutor->getRegistry());
    if (!_postprocessOutputTextureCompat) {
        return inputTexture;
    }

    return _postprocessOutputTextureCompat.get();
}
} // namespace ya
