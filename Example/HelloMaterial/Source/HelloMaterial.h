#pragma once

#include "Core/App/App.h"


struct HelloMaterial : public ya::App
{
    using Super = ya::App;
    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);
    }
    void onQuit() override
    {
        Super::onQuit();
    }
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