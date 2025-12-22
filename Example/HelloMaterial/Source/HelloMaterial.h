#pragma once

#include "Core/App/App.h"
#include "Editor/EditorLayer.h"
#include "Render/Mesh.h"
#include "Scene/SceneManager.h"
#include <imgui.h>

struct HelloMaterial : public ya::App
{
    using Super = ya::App;

    // Application-specific resources
    std::shared_ptr<ya::Mesh> cubeMesh;

    ya::Entity *_pointLightEntity = nullptr;
    ya::Entity *_litTestEntity    = nullptr;

    // Editor layer

    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);
    }

    void onPostInit() override
    {
        Super::onPostInit();
    }

    void onQuit() override
    {
        Super::onQuit();
    }



    void onSceneInit(ya::Scene *scene) override
    {
        Super::onSceneInit(scene);

        // HelloMaterial-specific initialization
        createCubeMesh();
        loadTextures();
        createMaterials();
        createEntities(scene);

        // Initialize editor layer

        YA_INFO("HelloMaterial scene initialized.");
    }

    void onSceneDestroy(ya::Scene *scene) override
    {
        cubeMesh.reset();

        Super::onSceneDestroy(scene);
    }

    // Application-specific methods implemented in HelloMaterial.cpp
    void createCubeMesh();
    void loadTextures();
    void createMaterials();
    void createEntities(ya::Scene *scene);

    void onUpdate(float dt) override;

    void onRender(float dt) override
    {
        Super::onRender(dt);
    }

    void onRenderGUI(float dt) override
    {
        Super::onRenderGUI(dt);
    }

    int onEvent(const ya::Event &event) override
    {
        Super::onEvent(event);
        return 0;
    }
};
