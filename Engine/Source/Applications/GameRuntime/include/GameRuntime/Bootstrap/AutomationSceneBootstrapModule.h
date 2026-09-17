#pragma once

#include "App/Module/Module.h"
#include "GameRuntime/IRuntimeModule.h"

#include <memory>
#include <string>

namespace ya
{

struct AutomationSceneBootstrapModule final : IModule, IRuntimeModule
{
    explicit AutomationSceneBootstrapModule(std::string defaultAutomationScenePath);

    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
    void* queryInterface(FInterfaceId interfaceId) override
    {
        return interfaceId == YA_RUNTIME_MODULE_INTERFACE ? static_cast<IRuntimeModule*>(this) : nullptr;
    }
    void onConfigure(App& app, AppDesc& desc) override;

  private:
    std::string _defaultAutomationScenePath;
};

[[nodiscard]] std::unique_ptr<IModule> createAutomationSceneBootstrapModule(std::string defaultAutomationScenePath);

} // namespace ya
