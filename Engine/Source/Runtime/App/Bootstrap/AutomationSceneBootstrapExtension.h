#pragma once

#include "Runtime/App/IAppExtension.h"

#include <string>

namespace ya
{

struct AutomationSceneBootstrapExtension final : IAppExtension
{
    explicit AutomationSceneBootstrapExtension(std::string defaultAutomationScenePath);

    void onConfigure(App& app, AppDesc& desc) override;

  private:
    std::string _defaultAutomationScenePath;
};

[[nodiscard]] std::unique_ptr<IAppExtension> createAutomationSceneBootstrapExtension(std::string defaultAutomationScenePath);

} // namespace ya
