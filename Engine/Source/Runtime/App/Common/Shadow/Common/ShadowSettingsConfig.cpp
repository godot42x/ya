#include "ShadowSettingsConfig.h"

#include "Config/ConfigManager.h"
#include "Core/Log.h"
#include "Runtime/App/App.h"

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

std::string toLowerCopy(std::string_view text)
{
    std::string normalized(text);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

bool tryParseShadowQualityValue(std::string_view text, EShadowQuality::T& outQuality)
{
    const std::string normalized = toLowerCopy(text);
    if (normalized == "off") {
        outQuality = EShadowQuality::Off;
        return true;
    }
    if (normalized == "low") {
        outQuality = EShadowQuality::Low;
        return true;
    }
    if (normalized == "medium") {
        outQuality = EShadowQuality::Medium;
        return true;
    }
    if (normalized == "high") {
        outQuality = EShadowQuality::High;
        return true;
    }
    if (normalized == "ultra") {
        outQuality = EShadowQuality::Ultra;
        return true;
    }
    return false;
}

bool tryParseShadowFilterValue(std::string_view text, EShadowFilter::T& outFilter)
{
    const std::string normalized = toLowerCopy(text);
    if (normalized == "hard") {
        outFilter = EShadowFilter::Hard;
        return true;
    }
    if (normalized == "pcf_low" || normalized == "pcflow" || normalized == "pcf-low") {
        outFilter = EShadowFilter::PCF_Low;
        return true;
    }
    if (normalized == "pcf_high" || normalized == "pcfhigh" || normalized == "pcf-high") {
        outFilter = EShadowFilter::PCF_High;
        return true;
    }
    return false;
}

} // namespace shadow_settings_config_detail

namespace shadow_settings
{

void loadAutomationOverridesFromConfig(AppAutomationShadowOverrides& overrides)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    if (std::string qualityText; configManager.tryGet<std::string>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.quality", qualityText)) {
        EShadowQuality::T quality = EShadowQuality::Medium;
        if (shadow_settings_config_detail::tryParseShadowQualityValue(qualityText, quality)) {
            overrides.quality = quality;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override: {}", qualityText);
        }
    }
    else if (uint32_t qualityValue = 0; configManager.tryGet<uint32_t>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.quality", qualityValue)) {
        if (qualityValue <= static_cast<uint32_t>(EShadowQuality::Ultra)) {
            overrides.quality = static_cast<EShadowQuality::T>(qualityValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override value: {}", qualityValue);
        }
    }

    if (bool directionalEnabled = false; configManager.tryGet<bool>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalEnabled", directionalEnabled)) {
        overrides.directionalEnabled = directionalEnabled;
    }
    if (uint32_t resolution = 0; configManager.tryGet<uint32_t>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.resolution", resolution)) {
        overrides.resolution = std::clamp(resolution, 128u, 8192u);
    }
    if (bool pointLightEnabled = false; configManager.tryGet<bool>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightEnabled", pointLightEnabled)) {
        overrides.pointLightEnabled = pointLightEnabled;
    }
    if (bool pointLightUseIndirect = false; configManager.tryGet<bool>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightUseIndirect", pointLightUseIndirect)) {
        overrides.pointLightUseIndirect = pointLightUseIndirect;
    }
    if (bool pointLightIndirectCullEnabled = false; configManager.tryGet<bool>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightIndirectCullEnabled", pointLightIndirectCullEnabled)) {
        overrides.pointLightIndirectCullEnabled = pointLightIndirectCullEnabled;
    }
    if (uint32_t maxPointLightShadows = 0; configManager.tryGet<uint32_t>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.maxPointLightShadows", maxPointLightShadows)) {
        overrides.maxPointLightShadows = std::min(maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }

    if (std::string filterText; configManager.tryGet<std::string>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.filter", filterText)) {
        EShadowFilter::T filter = EShadowFilter::Hard;
        if (shadow_settings_config_detail::tryParseShadowFilterValue(filterText, filter)) {
            overrides.filter = filter;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override: {}", filterText);
        }
    }
    else if (uint32_t filterValue = 0; configManager.tryGet<uint32_t>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.filter", filterValue)) {
        if (filterValue <= static_cast<uint32_t>(EShadowFilter::PCF_High)) {
            overrides.filter = static_cast<EShadowFilter::T>(filterValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override value: {}", filterValue);
        }
    }

    if (float bias = 0.0f; configManager.tryGet<float>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.bias", bias)) {
        overrides.bias = bias;
    }
    if (float normalBias = 0.0f; configManager.tryGet<float>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.normalBias", normalBias)) {
        overrides.normalBias = normalBias;
    }
    if (float directionalDistance = 0.0f; configManager.tryGet<float>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalDistance", directionalDistance)) {
        overrides.directionalDistance = directionalDistance;
    }
    if (uint32_t directionalCascades = 0; configManager.tryGet<uint32_t>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalCascades", directionalCascades)) {
        overrides.directionalCascades = std::clamp(directionalCascades, 1u, static_cast<uint32_t>(MAX_DIRECTIONAL_CASCADES));
    }
    if (std::array<float, MAX_DIRECTIONAL_CASCADES - 1> splitRatios{};
        configManager.tryGet(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalCascadeSplitRatios", splitRatios)) {
        overrides.directionalCascadeSplitRatios = splitRatios;
    }
    if (float depthRangeMultiplier = 0.0f; configManager.tryGet<float>(shadow_settings_config_detail::AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalDepthRangeMultiplier", depthRangeMultiplier)) {
        overrides.directionalDepthRangeMultiplier = std::max(depthRangeMultiplier, 1.0f);
    }
}

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
