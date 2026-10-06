#include "GameRuntime/Script/GameplayLua.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "LuaWidgetScripts.h"

#include "Script/Lua/LuaBackend.h"

#include <memory>

namespace ya
{

void bindGameplayLua(IWorldScriptRuntime& runtime, GameUIHost& ui, GameplayScriptContext)
{
    // Module functions were projected when the state was built. The Input
    // usertype replaces that table, so the key handler is put back here.
    attachInputSetKeyHandler(runtime);
    ui.setBehaviorRuntime(std::make_unique<LuaWidgetScripts>(runtime, ui));
}

} // namespace ya
