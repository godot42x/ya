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

    // void onSceneInit(ya::Scene *scene) override
    // {
    //     Super::onSceneInit(scene);

    //     YA_INFO("GreedySnake scene initialized.");
    // }

    void onSceneDestroy(ya::Scene *scene) override
    {
        Super::onSceneDestroy(scene);
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
