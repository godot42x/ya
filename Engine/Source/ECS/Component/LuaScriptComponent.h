
#pragma once

#include "Core/Base.h"

#include "Core/App/App.h"
#include "Scene/SceneManager.h"

#include <sol/sol.hpp>


namespace ya
{

struct LuaScriptComponent : public IComponent
{
    std::string scriptPath;
    bool        bLoaded = false;

    sol::table    self;
    sol::function onInit;
    sol::function onUpdate;
    sol::function onDestroy;
};



} // namespace ya