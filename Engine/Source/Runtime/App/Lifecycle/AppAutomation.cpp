#include "Runtime/App/Lifecycle/AppAutomation.h"

#include "Runtime/App/App.h"

#include "Core/Log.h"

namespace ya
{
namespace
{
bool shouldRequestQuitAfterFrame(const App& app)
{
    const AppAutomationOptions& automation = app.getDesc().automation;
    return automation.exitAfterFrame > 0 && app.getFrameIndex() >= automation.exitAfterFrame;
}
} // namespace

bool AppAutomation::shouldDeferQuit(const App& app)
{
    (void)app;
    return false;
}

void AppAutomation::applyStartupOverrides(AppDesc& appDesc)
{
    if (appDesc.automation.scenePath) {
        appDesc.defaultScenePath = appDesc.automation.scenePath;
    }
}

void AppAutomation::onFrameCompleted(App& app)
{
    if (!shouldRequestQuitAfterFrame(app)) {
        return;
    }

    YA_CORE_INFO("Automation requested graceful shutdown after frame {}", app.getDesc().automation.exitAfterFrame);
    app.requestQuit();
}

} // namespace ya
