#pragma once

#include "Core/Module/Module.h"

#include <memory>
#include <string>

namespace ya
{

struct AutomationSceneBootstrapModule final : IModule
{
    explicit AutomationSceneBootstrapModule(std::string defaultAutomationScenePath);

    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
    void onConfigure(App& app, AppDesc& desc) override;

  private:
    std::string _defaultAutomationScenePath;
};

[[nodiscard]] std::unique_ptr<IModule> createAutomationSceneBootstrapModule(std::string defaultAutomationScenePath);

} // namespace ya
