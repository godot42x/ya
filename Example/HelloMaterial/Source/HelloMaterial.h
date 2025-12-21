#pragma once

#include "Core/App/App.h"
#include "Editor/SceneHierarchyPanel.h"
#include "Render/Mesh.h"
#include "Scene/SceneManager.h"



struct HelloMaterial : public ya::App
{
    using Super = ya::App;

    // Application-specific resources
    std::shared_ptr<ya::Mesh> cubeMesh;

    ya::Entity *_pointLightEntity = nullptr;
    ya::Entity *_litTestEntity    = nullptr;

    // Editor
    std::unique_ptr<ya::SceneHierarchyPanel> _sceneHierarchyPanel;

    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);
    }

    void onPostInit() override
    {
        Super::onPostInit();
        // temp codes to disable other material systems except LitMaterialSystem
        // getRenderTarget()->forEachMaterialSystem(
        //     [](std::shared_ptr<ya::IMaterialSystem> system) {
        //         if (system) {
        //             if (system->_label != "LitMaterialSystem") {
        //                 system->bEnabled = false;
        //             }
        //         }
        //     });
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

        // Initialize editor
        _sceneHierarchyPanel = std::make_unique<ya::SceneHierarchyPanel>(scene);

        YA_INFO("HelloMaterial scene initialized.");
    }

    void onSceneDestroy(ya::Scene *scene) override
    {
        // Clean up application-specific resources before scene destruction
        _sceneHierarchyPanel.reset();
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

    void onRenderGUI() override
    {
        Super::onRenderGUI();

        // Render scene hierarchy panel
        if (_sceneHierarchyPanel)
        {
            _sceneHierarchyPanel->onImGuiRender();
        }

        // if (!ImGui::Begin("HelloMaterial")) {
        //     return;
        // ImGui::End();
        // }
        // ImGui::End();
    }

    int onEvent(const ya::Event &event) override
    {
        Super::onEvent(event);
        return 0;
    }
};
