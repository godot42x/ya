#pragma once

#include "Render/Shadow/ShadowSettings.h"

#include <string>

namespace ya
{

struct AppAutomationShadowOverrides;

namespace shadow_settings
{

void           loadAutomationOverridesFromConfig(AppAutomationShadowOverrides& overrides);
void           applyAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings);
[[nodiscard]] bool hasRuntimeSettings();
[[nodiscard]] ShadowSettings loadSettingsFromDocument(const std::string& documentName, const ShadowSettings& baseline);
[[nodiscard]] ShadowSettings loadRuntimeSettings(const ShadowSettings& baseline);
void saveRuntimeSettings(const ShadowSettings& shadowSettings);

} // namespace shadow_settings

} // namespace ya
