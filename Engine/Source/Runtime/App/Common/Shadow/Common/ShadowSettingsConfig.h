#pragma once

#include "Render/Shadow/ShadowSettings.h"

namespace ya
{

struct AppAutomationShadowOverrides;

namespace shadow_settings
{

void           loadAutomationOverridesFromConfig(AppAutomationShadowOverrides& overrides);
void           applyAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings);
ShadowSettings loadEditorSettings(const ShadowSettings& baseline, const AppAutomationShadowOverrides* automationOverrides = nullptr);
void           saveEditorSettings(const ShadowSettings& shadowSettings);

} // namespace shadow_settings

} // namespace ya
