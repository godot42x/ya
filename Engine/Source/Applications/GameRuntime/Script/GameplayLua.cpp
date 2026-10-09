#include "GameRuntime/Script/GameplayLua.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "LuaWidgetScripts.h"

#include <memory>

namespace ya
{

void bindGameplayLua(IWorldScriptRuntime& runtime, GameUIHost& ui, GameplayScriptContext)
{
    ui.setBehaviorRuntime(std::make_unique<LuaWidgetScripts>(runtime, ui));
}

} // namespace ya
