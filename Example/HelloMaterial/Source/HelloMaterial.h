#pragma once

#include "Core/App/App.h"
#include "Render/Mesh.h"


struct HelloMaterial : public ya::App
{
    using Super = ya::App;

    // Application-specific resources
    std::shared_ptr<ya::Mesh>   cubeMesh;
    std::vector<ya::Material *> materials;

    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);
    }

    void onQuit() override
    {
        // Note: cubeMesh and materials are cleaned up in onSceneDestroy()
        Super::onQuit();
    }

    void onSceneInit(ya::Scene *scene) override
    {
        // Call base class to set up camera
        Super::onSceneInit(scene);

        // HelloMaterial-specific initialization
        createCubeMesh();
        loadTextures();
        createMaterials();
        createEntities(scene);
    }

    void onSceneDestroy(ya::Scene *scene) override
    {
        // Clean up application-specific resources before scene destruction
        cubeMesh.reset();
        materials.clear(); // TODO: materials not managed by the scene

        Super::onSceneDestroy(scene);
    }

    // Application-specific methods implemented in HelloMaterial.cpp
    void createCubeMesh();
    void loadTextures();
    void createMaterials();
    void createEntities(ya::Scene *scene);

    void onUpdate(float dt) override
    {
        Super::onUpdate(dt);
    }

    void onRender(float dt) override
    {
        Super::onRender(dt);
    }

    void onRenderGUI() override
    {
        Super::onRenderGUI();
    }

    int onEvent(const ya::Event &event) override
    {
        Super::onEvent(event);
        return 0;
    }
};