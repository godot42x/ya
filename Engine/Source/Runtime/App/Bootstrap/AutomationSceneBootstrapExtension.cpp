#include "Runtime/App/Bootstrap/AutomationSceneBootstrapExtension.h"

#include "Runtime/App/App.h"

namespace ya
{

AutomationSceneBootstrapExtension::AutomationSceneBootstrapExtension(std::string defaultAutomationScenePath)
    : _defaultAutomationScenePath(std::move(defaultAutomationScenePath))
{
}

void AutomationSceneBootstrapExtension::onConfigure(App& app, AppDesc& desc)
{
    (void)app;

    if (_defaultAutomationScenePath.empty()) {
        return;
    }
    if (desc.automation.scenePath.has_value()) {
        return;
    }
    if (desc.automation.exitAfterFrame == 0) {
        return;
    }

    desc.automation.scenePath = _defaultAutomationScenePath;
}

std::unique_ptr<IAppExtension> createAutomationSceneBootstrapExtension(std::string defaultAutomationScenePath)
{
    return std::make_unique<AutomationSceneBootstrapExtension>(std::move(defaultAutomationScenePath));
}

} // namespace ya
