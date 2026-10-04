#pragma once

// Test-only door onto the Lua backend. Includes sol. Product code does not
// include this header. GameRuntime still includes the concrete headers
// directly until B2-5 hides the system behind IWorldScriptRuntime.
#include "Script/Lua/LuaScriptBinding.h"
#include "Script/Lua/LuaScriptingSystem.h"

#include <sol/sol.hpp>
