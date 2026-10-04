#pragma once

#include "ECS/Systems/ScriptPropertyPreview.h"

namespace ya
{

/// Heap object. `installLuaScriptBackend` creates one for the process and
/// does not destroy it. `destroyScriptPropertyPreview` is for a caller that
/// owns a private instance: the Lua state must die in this module.
[[nodiscard]] YA_SCRIPT_LUA_API IScriptPropertyPreview* createScriptPropertyPreview();
YA_SCRIPT_LUA_API void destroyScriptPropertyPreview(IScriptPropertyPreview* preview);

} // namespace ya
