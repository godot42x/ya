#pragma once

#include "Core/Api.h"

namespace ya
{

/// Install the Lua world-script runtime and the editor property preview.
/// Call once, from the process entry (YARuntime and the test runner), before
/// any App is constructed. A missing call leaves gameplay scripts off and
/// the inspector without property rows.
YA_SCRIPT_LUA_API void installLuaScriptBackend();

} // namespace ya
