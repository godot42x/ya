#include "GameEditor/EditorRuntimeSettings.h"

#include "Core/Config/ConfigManager.h"
#include "GameRuntime/Lifecycle/FPSCtrl.h"

#include <string_view>

namespace ya::editor_runtime_settings
{

namespace
{

constexpr const char* CONFIG_DOCUMENT = "editor";
constexpr const char* KEY_FPS_ENABLE  = "runtime.framePacing.enabled";
constexpr const char* KEY_FPS_LIMIT   = "runtime.framePacing.fpsLimit";

template <typename T>
void migrateLegacyRuntimeSetting(std::string_view key)
{
    auto& config = ConfigManager::get();
    if (config.hasValue("runtime", key)) {
        return;
    }

    T value{};
    if (config.tryGet<T>("editor", key, value)) {
        ConfigManager::Editor("runtime").set(key, value);
    }
}

} // namespace

void load()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    auto* fpsControl    = FPSControl::get();
    fpsControl->bEnable = config.getOr<bool>(CONFIG_DOCUMENT, KEY_FPS_ENABLE, fpsControl->bEnable);
    fpsControl->setFPSLimit(config.getOr<float>(CONFIG_DOCUMENT, KEY_FPS_LIMIT, fpsControl->fpsLimit));
}

void migrateLegacy()
{
    migrateLegacyRuntimeSetting<bool>("render.deferred.reverseViewportY");
    migrateLegacyRuntimeSetting<bool>("render.deferred.ssaoEnabled");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.radius");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.bias");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.power");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.intensity");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRDiffuseIBL");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRSpecularIBL");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.inversion");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.grayscale");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.kernel");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.kernelTexelOffset");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.tonemapping.enabled");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.tonemapping.curve");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.tonemapping.exposure");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.gammaCorrection");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.gamma");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.randomGrain");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.randomGrainStrength");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.bloom.enabled");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.threshold");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.softKnee");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.extractIntensity");
    migrateLegacyRuntimeSetting<int>("render.postprocess.bloom.blurPasses");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.strength");
}

void save()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    const auto* fpsControl = FPSControl::get();
    ConfigManager::Editor(CONFIG_DOCUMENT)
        .set(KEY_FPS_ENABLE, fpsControl->bEnable)
        .set(KEY_FPS_LIMIT, fpsControl->fpsLimit)
        .flush();
}

} // namespace ya::editor_runtime_settings
