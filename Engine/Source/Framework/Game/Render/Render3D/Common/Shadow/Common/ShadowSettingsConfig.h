#pragma once

#include "AppServices/ShadowSettings.h"

#include <string>

namespace ya
{

struct AppAutomationShadowOverrides;

namespace shadow_settings
{

YA_RENDER_3D_API void           applyAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings);
[[nodiscard]] YA_RENDER_3D_API bool hasRuntimeSettings();
[[nodiscard]] YA_RENDER_3D_API ShadowSettings loadSettingsFromDocument(const std::string& documentName, const ShadowSettings& baseline);
[[nodiscard]] YA_RENDER_3D_API ShadowSettings loadRuntimeSettings(const ShadowSettings& baseline);
YA_RENDER_3D_API void saveRuntimeSettings(const ShadowSettings& shadowSettings);

} // namespace shadow_settings

} // namespace ya
