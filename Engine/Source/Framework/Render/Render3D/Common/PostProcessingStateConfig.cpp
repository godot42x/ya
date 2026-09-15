#include "PostProcessingStateConfig.h"

#include "Core/Config/ConfigManager.h"

#include <algorithm>

namespace ya::postprocess_settings
{

namespace
{

constexpr const char* RUNTIME_CONFIG_DOCUMENT = "runtime";

} // namespace

PostProcessingState loadRuntimeSettings(const PostProcessingState& baseline)
{
    PostProcessingState post = baseline;
    auto& config = ConfigManager::get();
    post.bEnableInversion = config.getOr<bool>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.inversion", post.bEnableInversion);
    post.grayscaleMode = static_cast<PostProcessingState::EGrayscaleMode>(config.getOr<int>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.grayscale", static_cast<int>(post.grayscaleMode)));
    post.kernelMode = static_cast<PostProcessingState::EKernelMode>(config.getOr<int>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernel", static_cast<int>(post.kernelMode)));
    post.kernelTexelOffset = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernelTexelOffset", post.kernelTexelOffset);
    post.bEnableToneMapping = config.getOr<bool>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.enabled", post.bEnableToneMapping);
    post.toneMappingCurve = static_cast<PostProcessingState::EToneMappingCurve>(config.getOr<int>(
        RUNTIME_CONFIG_DOCUMENT,
        "render.postprocess.basic.tonemapping.curve",
        static_cast<int>(post.toneMappingCurve)));
    post.exposure = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.exposure", post.exposure);
    post.bEnableGammaCorrection = config.getOr<bool>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gammaCorrection", post.bEnableGammaCorrection);
    post.gamma = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gamma", post.gamma);
    post.bEnableRandomGrain = config.getOr<bool>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.randomGrain", post.bEnableRandomGrain);
    post.randomGrainStrength = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.randomGrainStrength", post.randomGrainStrength);
    post.bEnableBloom = config.getOr<bool>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.enabled", post.bEnableBloom);
    post.bloomThreshold = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.threshold", post.bloomThreshold);
    post.bloomSoftKnee = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.softKnee", post.bloomSoftKnee);
    post.bloomExtractIntensity = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.extractIntensity", post.bloomExtractIntensity);
    post.bloomBlurPasses = static_cast<uint32_t>(std::max(
        1,
        config.getOr<int>(RUNTIME_CONFIG_DOCUMENT,
                          "render.postprocess.bloom.blurPasses",
                          static_cast<int>(post.bloomBlurPasses))));
    post.bloomStrength = config.getOr<float>(
        RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.strength", post.bloomStrength);
    return post;
}

void saveRuntimeSettings(const PostProcessingState& settings)
{
    auto& config = ConfigManager::get();
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.inversion", settings.bEnableInversion);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.grayscale", static_cast<int>(settings.grayscaleMode));
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernel", static_cast<int>(settings.kernelMode));
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernelTexelOffset", settings.kernelTexelOffset);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.enabled", settings.bEnableToneMapping);
    config.set(RUNTIME_CONFIG_DOCUMENT,
               "render.postprocess.basic.tonemapping.curve",
               static_cast<int>(settings.toneMappingCurve));
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.exposure", settings.exposure);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gammaCorrection", settings.bEnableGammaCorrection);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gamma", settings.gamma);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.randomGrain", settings.bEnableRandomGrain);
    config.set(RUNTIME_CONFIG_DOCUMENT,
               "render.postprocess.basic.output.randomGrainStrength",
               settings.randomGrainStrength);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.enabled", settings.bEnableBloom);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.threshold", settings.bloomThreshold);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.softKnee", settings.bloomSoftKnee);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.extractIntensity", settings.bloomExtractIntensity);
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.blurPasses", static_cast<int>(settings.bloomBlurPasses));
    config.set(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.strength", settings.bloomStrength);
    config.flushDocument(RUNTIME_CONFIG_DOCUMENT);
}

} // namespace ya::postprocess_settings
