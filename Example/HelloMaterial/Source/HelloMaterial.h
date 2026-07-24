#pragma once

#include "Core/Module/Module.h"
#include "Render/Mesh.h"
#include "Runtime/Application/AppState.h"

struct HelloMaterialModule final : ya::IModule
{
    std::shared_ptr<ya::Mesh> cubeMesh;

    std::vector<std::string> _pongMaterialNames;

    bool onLoad(ya::FModuleContext&) override { return true; }
    bool onStart(const ya::FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}

    void onAttach(ya::App&) override
    {
        createCubeMesh();
        loadResources();
    }

    void onDetach(ya::App&) override { cubeMesh.reset(); }

    void onSceneActivated(ya::App&, ya::Scene*) override
    {
        YA_INFO("HelloMaterial scene initialized.");
    }

    void createCubeMesh();
    void loadResources();
    void createMaterials();
    void createEntities(ya::Scene* scene);
};
