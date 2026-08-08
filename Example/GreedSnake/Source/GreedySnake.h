#pragma once

#include "Foundation/Core/Module/Module.h"

struct GreedySnakeModule final : ya::IModule
{
    bool onLoad(ya::FModuleContext&) override
    {
        return true;
    }

    bool onStart(const ya::FEngineContext&) override
    {
        return true;
    }

    void onStop() override {}
    void onUnload() override {}
};
