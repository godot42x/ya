#pragma once

#include "Core/App/App.h"
#include "Render/Mesh.h"


struct GreedySnake : public ya::App
{
    using Super = ya::App;


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

        YA_INFO("GreedySnake scene initialized.");
    }

    void onSceneDestroy(ya::Scene *scene) override
    {
        Super::onSceneDestroy(scene);
    }

    // Application-specific methods implemented in HelloMaterial.cpp
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
