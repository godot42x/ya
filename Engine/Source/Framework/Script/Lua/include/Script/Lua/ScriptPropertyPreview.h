#pragma once

#include "ECS/Systems/ScriptPropertyPreview.h"

namespace ya
{

/// Heap object owned by the caller (App, until B2-5's install). Destroy it
/// with `destroyScriptPropertyPreview` in this module so the Lua state dies
/// here, not in another DLL and not from a process-lifetime static.
[[nodiscard]] YA_SCRIPT_LUA_API IScriptPropertyPreview* createScriptPropertyPreview();
YA_SCRIPT_LUA_API void destroyScriptPropertyPreview(IScriptPropertyPreview* preview);

} // namespace ya
