#pragma once

#include "Foundation/Core/Api.h"

namespace ya
{

struct ScriptApiRegistry;

/**
 * @brief Registers the `asset.*` function library (EditAssetLibrary-style).
 *
 * Every registered command is callable from JS as `ya.asset.<name>(...)` and
 * from external tools over the automation RPC (`invoke` / MCP bridge) with
 * the exact same JSON contract - a single catalog, two surfaces.
 */
YA_CORE_API void registerAssetScriptApis(ScriptApiRegistry& registry);

} // namespace ya
