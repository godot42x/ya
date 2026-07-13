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
constexpr const char* DEFERRED_PIPELINE_CONFIG_DOC_NAME                  = "editor";
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
}

ShadowSettings loadEditorSettings(const ShadowSettings& baseline, const AppAutomationShadowOverrides* automationOverrides)
{
    auto& cfgManager = ConfigManager::get();

    const bool bEnableShadowMapping = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                             shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING,
                                                             baseline.isEnabled());
    const bool bEnablePointLightShadow = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW,
                                                                baseline.pointLightEnabled);

    ShadowSettings shadowSettings = baseline;
    int qualityValue = cfgManager.getOr<int>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                             shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY,
                                             static_cast<int>(shadowSettings.quality));
    qualityValue = std::clamp(qualityValue,
                              static_cast<int>(EShadowQuality::Off),
                              static_cast<int>(EShadowQuality::Ultra));
    shadowSettings.quality = static_cast<EShadowQuality::T>(qualityValue);
    shadowSettings.directionalEnabled = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                               shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED,
                                                               shadowSettings.directionalEnabled);
    shadowSettings.pointLightEnabled = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED,
                                                              bEnablePointLightShadow);
    shadowSettings.pointLightUseIndirect = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                  shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT,
                                                                  shadowSettings.pointLightUseIndirect);
    shadowSettings.pointLightIndirectCullEnabled = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                          shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL,
                                                                          shadowSettings.pointLightIndirectCullEnabled);
    shadowSettings.maxPointLightShadows = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
                              static_cast<int>(shadowSettings.maxPointLightShadows)),
        0,
        static_cast<int>(MAX_POINT_LIGHTS)));
    shadowSettings.resolution = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION,
                              static_cast<int>(shadowSettings.resolution)),
        128,
        8192));
    shadowSettings.filter = static_cast<EShadowFilter::T>(std::clamp(
        cfgManager.getOr<int>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER,
                              static_cast<int>(shadowSettings.filter)),
        static_cast<int>(EShadowFilter::Hard),
        static_cast<int>(EShadowFilter::PCF_High)));
    shadowSettings.bias = cfgManager.getOr<float>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                  shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS,
                                                  shadowSettings.bias);
    shadowSettings.normalBias = cfgManager.getOr<float>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                        shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS,
                                                        shadowSettings.normalBias);
    shadowSettings.directionalDistance = cfgManager.getOr<float>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                 shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST,
                                                                 shadowSettings.directionalDistance);
    shadowSettings.directionalStableFit = cfgManager.getOr<bool>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                 shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE,
                                                                 shadowSettings.directionalStableFit);
    shadowSettings.directionalCascades = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE,
                              static_cast<int>(shadowSettings.directionalCascades)),
        0,
        4));

    if (!bEnableShadowMapping || shadowSettings.quality == EShadowQuality::Off) {
        shadowSettings.quality = EShadowQuality::Off;
    }
    shadowSettings.pointLightEnabled    = bEnablePointLightShadow && shadowSettings.pointLightEnabled;
    shadowSettings.maxPointLightShadows = std::min(shadowSettings.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    if (automationOverrides) {
        applyAutomationOverrides(*automationOverrides, shadowSettings);
    }

    return shadowSettings;
}

void saveEditorSettings(const ShadowSettings& shadowSettings)
{
    ConfigManager::Editor(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_DOC_NAME)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING, shadowSettings.isEnabled())
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW, shadowSettings.pointLightEnabled)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
             static_cast<int>(std::min(shadowSettings.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS))))
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY, static_cast<int>(shadowSettings.quality))
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED, shadowSettings.directionalEnabled)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED, shadowSettings.pointLightEnabled)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT, shadowSettings.pointLightUseIndirect)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL, shadowSettings.pointLightIndirectCullEnabled)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION, static_cast<int>(shadowSettings.resolution))
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER, static_cast<int>(shadowSettings.filter))
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS, shadowSettings.bias)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS, shadowSettings.normalBias)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST, shadowSettings.directionalDistance)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE, shadowSettings.directionalStableFit)
        .set(shadow_settings_config_detail::DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE, static_cast<int>(shadowSettings.directionalCascades));
}

} // namespace shadow_settings

} // namespace ya
