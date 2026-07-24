#include "Runtime/Application/Bootstrap/AutomationSceneBootstrapModule.h"

#include "Runtime/Application/App.h"

namespace ya
{

AutomationSceneBootstrapModule::AutomationSceneBootstrapModule(std::string defaultAutomationScenePath)
    : _defaultAutomationScenePath(std::move(defaultAutomationScenePath))
{
}

void AutomationSceneBootstrapModule::onConfigure(App& app, AppDesc& desc)
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

std::unique_ptr<IModule> createAutomationSceneBootstrapModule(std::string defaultAutomationScenePath)
{
    return std::make_unique<AutomationSceneBootstrapModule>(std::move(defaultAutomationScenePath));
}

} // namespace ya
