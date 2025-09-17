#pragma once

#include "Core/App/App.h"


struct HelloTexture : public ya::App
{
    void onInit(ya::AppDesc ci) override;
    void onQuit() override;

    void onUpdate(float dt) override;
    void onRender(float dt) override;
    int  onEvent(SDL_Event &event) override;
};