#pragma once

#include "Core/App/App.h"
#include "Core/System/VirtualFileSystem.h"
#include "Editor/EditorLayer.h"
#include "Render/Mesh.h"
#include "Scene/SceneManager.h"
#include <imgui.h>


struct HelloMaterial : public ya::App
{
    using Super = ya::App;

    // Application-specific resources
    std::shared_ptr<ya::Mesh> cubeMesh;

    ya::Entity              *_pointLightEntity = nullptr;
    ya::Entity              *_litTestEntity    = nullptr;
    std::vector<std::string> _pongMaterialNames;

    // Editor layer

    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);


        createCubeMesh();
        loadResources();
        VirtualFileSystem::get()->setGameRoot("Example/HelloMaterial");
    }

    void onPostInit() override
    {
        Super::onPostInit();
    }

    void onQuit() override
    {
        cubeMesh.reset();
        Super::onQuit();
    }



    // void onSceneInit(ya::Scene *scene) override
    // {
    //     Super::onSceneInit(scene);
    // }

    void onSceneActivated(ya::Scene *scene) override
    {
        Super::onSceneActivated(scene);
        // HelloMaterial-specific initialization
        static std::once_flag flag;
        std::call_once(flag, [&]() {
            // TODO: entities and components could be create in one scene in multiple times
            //      Remove this when you implement the:
            // 1. Scene serialization/deserialization
            // 2. Scene cloning
            // 3. Scene load/init/unload/destroy
            createMaterials();
            createEntities(scene);
        });

        // Initialize editor layer

        YA_INFO("HelloMaterial scene initialized.");
    }

    void onSceneDestroy(ya::Scene *scene) override
    {
        // cubeMesh.reset();

        Super::onSceneDestroy(scene);
    }

    // Application-specific methods implemented in HelloMaterial.cpp
    void createCubeMesh();
    void loadResources();
    void createMaterials();
    void createEntities(ya::Scene *scene);

    void onUpdate(float dt) override;

    void onRender(float dt) override
    {
        Super::onRender(dt);
    }

    void onRenderGUI(float dt) override;

    int onEvent(const ya::Event &event) override
    {
        Super::onEvent(event);
        return 0;
    }

    void onEnterRuntime() override;
};
