
#include "ScriptingSystem.h"
#include <sol/sol.hpp>


struct Class;

namespace ya
{
struct LuaScriptingSystem : public ScriptingSystem
{
    sol::state _lua;

    void init() override;
    void onUpdate(float deltaTime) override;
    void onStop();

  private:
    // 自动绑定所有已注册的反射组件到Lua
    void bindReflectedComponents();

    // 为单个组件类型注册Lua绑定
    void bindComponentType(Class *classInfo);
};



} // namespace ya