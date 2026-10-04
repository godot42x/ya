#pragma once

#include "ECS/Systems/Components/LuaScriptComponent.h"

#include <sol/sol.hpp>

namespace ya
{

/// Editor-only Lua state used to preview script properties without running
/// gameplay callbacks. One state is enough: scripts share package.path the
/// way the runtime does, so `require` resolves the same modules.
class EditorLuaPreview
{
    sol::state _lua;
    bool       _bReady = false;

    void ensureReady();

  public:
    /// Execute `script.scriptPath` in the editor state and copy `_PROPERTIES`
    /// into C++ rows. Never leaves sol handles on `script` — the preview
    /// table dies with this function, while `_lua` is still alive.
    bool load(LuaScriptInstance& script);

    /// Preview load does not leave sol handles on `component`. Kept so section
    /// teardown still has a hook before `~EditorLuaPreview`.
    void releaseMatching(LuaScriptComponent& component);
};

} // namespace ya
