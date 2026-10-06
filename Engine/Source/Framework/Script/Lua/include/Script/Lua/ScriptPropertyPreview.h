#pragma once

#include "ECS/Systems/ScriptPropertyPreview.h"

namespace ya
{

/// Heap object. `installLuaScriptBackend` creates one for the process;
/// `uninstallLuaScriptBackend` destroys it through `destroyScriptPropertyPreview`.
[[nodiscard]] YA_SCRIPT_LUA_API IScriptPropertyPreview* createScriptPropertyPreview();
YA_SCRIPT_LUA_API void destroyScriptPropertyPreview(IScriptPropertyPreview* preview);

} // namespace ya
