
#include "ScriptingSystem.h"
#include <sol/sol.hpp>


namespace ya
{
struct LuaScriptingSystem : public ScriptingSystem
{
    sol::state _lua;

    void init() override;
    void onUpdate(float deltaTime) override;
};



} // namespace ya