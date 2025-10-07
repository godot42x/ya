
#pragma once

#include "ECS/System.h"

namespace ya
{
struct ScriptingSystem : public ISystem
{
    virtual void init() = 0;
};



struct LuaScriptingSystem : public ScriptingSystem
{
    void init() override
    {

    }
    void onUpdate(float deltaTime) override;
};



} // namespace ya