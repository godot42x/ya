#include "ShadowSettingsConfig.h"

#include "Core/Config/ConfigManager.h"
#include "AppServices/AppAutomation.h"
#include "Core/Log.h"


#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ya
{

namespace shadow_settings_config_detail
{

constexpr const char* AUTOMATION_CONFIG_DOC_NAME                         = "automation";
constexpr const char* RUNTIME_CONFIG_DOC_NAME                            = "runtime";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING = "render.deferred.shadow.enableShadowMapping";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW = "render.deferred.shadow.enablePointLightShadow";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS   = "render.deferred.shadow.maxPointLightShadowCount";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY            = "render.deferred.shadow.quality";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED = "render.deferred.shadow.directionalEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED       = "render.deferred.shadow.pointLightEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT      = "render.deferred.shadow.pointLightUseIndirect";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL          = "render.deferred.shadow.pointLightIndirectCullEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION          = "render.deferred.shadow.resolution";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER              = "render.deferred.shadow.filter";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS                = "render.deferred.shadow.bias";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS         = "render.deferred.shadow.normalBias";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST    = "render.deferred.shadow.directionalDistance";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE = "render.deferred.shadow.directionalCascades";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE  = "render.deferred.shadow.directionalStableFit";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_SPLITS  = "render.deferred.shadow.directionalCascadeSplitRatios";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_Z_RANGE = "render.deferred.shadow.directionalDepthRangeMultiplier";

} // namespace shadow_settings_config_detail

namespace shadow_settings
{

void applyAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings)
{
    if (overrides.quality) {
        shadowSettings.applyQualityPreset(*overrides.quality);
    }
    if (overrides.directionalEnabled) {
        shadowSettings.directionalEnabled = *overrides.directionalEnabled;
    }
    if (overrides.resolution) {
        shadowSettings.resolution = std::clamp(*overrides.resolution, 128u, 8192u);
    }
    if (overrides.pointLightEnabled) {
        shadowSettings.pointLightEnabled = *overrides.pointLightEnabled;
    }
    if (overrides.pointLightUseIndirect) {
        shadowSettings.pointLightUseIndirect = *overrides.pointLightUseIndirect;
    }
    if (overrides.pointLightIndirectCullEnabled) {
        shadowSettings.pointLightIndirectCullEnabled = *overrides.pointLightIndirectCullEnabled;
    }
    if (overrides.maxPointLightShadows) {
        shadowSettings.maxPointLightShadows = std::min(*overrides.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }
    if (overrides.filter) {
        shadowSettings.filter = *overrides.filter;
    }
    if (overrides.bias) {
        shadowSettings.bias = *overrides.bias;
    }
    if (overrides.normalBias) {
        shadowSettings.normalBias = *overrides.normalBias;
    }
    if (overrides.directionalDistance) {
        shadowSettings.directionalDistance = *overrides.directionalDistance;
    }
    if (overrides.directionalCascades) {
        shadowSettings.directionalCascades = *overrides.directionalCascades;
        shadowSettings.resetDirectionalCascadeSplitRatios();
    }
    if (overrides.directionalCascadeSplitRatios) {
        shadowSettings.directionalCascadeSplitRatios = *overrides.directionalCascadeSplitRatios;
        shadowSettings.sanitizeDirectionalCascadeSplitRatios();
    }
    if (overrides.directionalDepthRangeMultiplier) {
        shadowSettings.directionalDepthRangeMultiplier = *overrides.directionalDepthRangeMultiplier;
    }
}

bool hasRuntimeSettings()
{
    return ConfigManager::get().hasValue(
        shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
        shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY);
}

ShadowSettings loadSettingsFromDocument(const std::string& documentName, const ShadowSettings& baseline)
{
    auto& config = ConfigManager::get();
    const bool bEnableShadowMapping = config.getOr<bool>(documentName,
                                                         shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING,
                                                         baseline.isEnabled());
    const bool bEnablePointLightShadow = config.getOr<bool>(documentName,
                                                            shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW,
                                                            baseline.pointLightEnabled);

    ShadowSettings settings = baseline;
    settings.quality = static_cast<EShadowQuality::T>(std::clamp(
        config.getOr<int>(documentName,
                          shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY,
                          static_cast<int>(settings.quality)),
        static_cast<int>(EShadowQuality::Off),
        static_cast<int>(EShadowQuality::Ultra)));
    settings.directionalEnabled = config.getOr<bool>(documentName,
                                                     shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED,
                                                     settings.directionalEnabled);
    settings.pointLightEnabled = config.getOr<bool>(documentName,
                                                    shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED,
                                                    bEnablePointLightShadow);
    settings.pointLightUseIndirect = config.getOr<bool>(documentName,
                                                        shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT,
                                                        settings.pointLightUseIndirect);
    settings.pointLightIndirectCullEnabled = config.getOr<bool>(documentName,
                                                                shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL,
                                                                settings.pointLightIndirectCullEnabled);
    settings.maxPointLightShadows = static_cast<uint32_t>(std::clamp(
        config.getOr<int>(documentName,
                          shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
                          static_cast<int>(settings.maxPointLightShadows)),
        0,
        static_cast<int>(MAX_POINT_LIGHTS)));
    settings.resolution = static_cast<uint32_t>(std::clamp(
        config.getOr<int>(documentName,
                          shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION,
                          static_cast<int>(settings.resolution)),
        128,
        8192));
    settings.filter = static_cast<EShadowFilter::T>(std::clamp(
        config.getOr<int>(documentName,
                          shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER,
                          static_cast<int>(settings.filter)),
        static_cast<int>(EShadowFilter::Hard),
        static_cast<int>(EShadowFilter::PCF_High)));
    settings.bias = config.getOr<float>(documentName,
                                        shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS,
                                        settings.bias);
    settings.normalBias = config.getOr<float>(documentName,
                                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS,
                                              settings.normalBias);
    settings.directionalDistance = config.getOr<float>(documentName,
                                                       shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST,
                                                       settings.directionalDistance);
    settings.directionalStableFit = config.getOr<bool>(documentName,
                                                       shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE,
                                                       settings.directionalStableFit);
    if (int configuredCascades = 0;
        config.tryGet(documentName,
                      shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE,
                      configuredCascades)) {
        settings.directionalCascades = static_cast<uint32_t>(std::clamp(
            configuredCascades, 1, static_cast<int>(MAX_DIRECTIONAL_CASCADES)));
        settings.resetDirectionalCascadeSplitRatios();
    }
    settings.directionalCascadeSplitRatios = config.getOr(
        documentName,
        shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_SPLITS,
        settings.directionalCascadeSplitRatios);
    settings.sanitizeDirectionalCascadeSplitRatios();
    settings.directionalDepthRangeMultiplier = std::max(
        config.getOr<float>(documentName,
                            shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_Z_RANGE,
                            settings.directionalDepthRangeMultiplier),
        1.0f);

    if (!bEnableShadowMapping || settings.quality == EShadowQuality::Off) {
        settings.quality = EShadowQuality::Off;
    }
    settings.pointLightEnabled = bEnablePointLightShadow && settings.pointLightEnabled;
    return settings;
}

ShadowSettings loadRuntimeSettings(const ShadowSettings& baseline)
{
    return loadSettingsFromDocument(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME, baseline);
}

void saveRuntimeSettings(const ShadowSettings& settings)
{
    auto& config = ConfigManager::get();
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING,
               settings.isEnabled());
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW,
               settings.pointLightEnabled);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
               static_cast<int>(std::min(settings.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS))));
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY,
               static_cast<int>(settings.quality));
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED,
               settings.directionalEnabled);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED,
               settings.pointLightEnabled);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT,
               settings.pointLightUseIndirect);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL,
               settings.pointLightIndirectCullEnabled);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION,
               static_cast<int>(settings.resolution));
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER,
               static_cast<int>(settings.filter));
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS,
               settings.bias);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS,
               settings.normalBias);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST,
               settings.directionalDistance);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE,
               settings.directionalStableFit);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE,
               static_cast<int>(settings.directionalCascades));
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_SPLITS,
               settings.directionalCascadeSplitRatios);
    config.set(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME,
               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_Z_RANGE,
               settings.directionalDepthRangeMultiplier);
    config.flushDocument(shadow_settings_config_detail::RUNTIME_CONFIG_DOC_NAME);
}

} // namespace shadow_settings

} // namespace ya
