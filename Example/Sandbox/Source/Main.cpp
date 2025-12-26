#pragma once

#include "Base.h"

#include "Core/App/App.h"


struct SANDBOX_API SandboxApp : public ya::App
{
    void onInit(ya::AppDesc ci) override;
    void onQuit() override;

    void onUpdate(float dt) override;
    void onRender(float dt) override;
};