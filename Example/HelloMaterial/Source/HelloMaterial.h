#pragma once

#include "App/Module/Module.h"
#include "GameRuntime/IRuntimeModule.h"
#include "Resource/Mesh.h"
#include "Core/Common/AppState.h"

namespace ya { struct IRender; }

struct HelloMaterialModule final : ya::IModule, ya::IRuntimeModule
{
    ya::IRender* render = nullptr;
    std::shared_ptr<ya::Mesh> cubeMesh;

    std::vector<std::string> _pongMaterialNames;

    bool onLoad(ya::FModuleContext&) override { return true; }
    bool onStart(const ya::FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
    void* queryInterface(ya::FInterfaceId interfaceId) override
    {
        return interfaceId == ya::YA_RUNTIME_MODULE_INTERFACE ? static_cast<ya::IRuntimeModule*>(this) : nullptr;
    }

    void onAttach(ya::App&) override;
    void onDetach(ya::App&) override;

    void onSceneActivated(ya::App&, ya::Scene*) override;

    void createCubeMesh();
    void loadResources();
    void createMaterials();
    void createEntities(ya::Scene* scene);
    void createUIDemo(ya::App& app, ya::Scene* scene);
};
