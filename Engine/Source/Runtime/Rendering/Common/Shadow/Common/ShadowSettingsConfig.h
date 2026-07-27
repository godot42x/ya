#pragma once

#include "Render/Shadow/ShadowSettings.h"

#include <string>

namespace ya
{

struct AppAutomationShadowOverrides;

namespace shadow_settings
{

ENGINE_API void           loadAutomationOverridesFromConfig(AppAutomationShadowOverrides& overrides);
ENGINE_API void           applyAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings);
[[nodiscard]] ENGINE_API bool hasRuntimeSettings();
[[nodiscard]] ENGINE_API ShadowSettings loadSettingsFromDocument(const std::string& documentName, const ShadowSettings& baseline);
[[nodiscard]] ENGINE_API ShadowSettings loadRuntimeSettings(const ShadowSettings& baseline);
ENGINE_API void saveRuntimeSettings(const ShadowSettings& shadowSettings);

} // namespace shadow_settings

} // namespace ya
