#pragma once

#include "Core/Api.h"
#include "Core/Scripting/ScriptValue.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

/// One editable script property: the C++ copy of a `_PROPERTIES` row, safe to
/// keep after the Lua state that produced it is gone.
struct LuaScriptProperty
{
    std::string         name;
    script::ScriptValue value;
    std::string         typeHint;
    float               min             = 0.0f;
    float               max             = 100.0f;
    std::string         tooltip         = "";
    std::string         serializedValue = "";
};

/// One script attached to one host. The authored half (path, enabled, order
/// override, property overrides) is what hosts serialize. Live Lua tables and
/// callbacks are not stored here: `LuaScriptingSystem` keeps them in a side
/// table keyed by (`lua_State*`, `runtimeId`). `runtimeId == 0` means no live
/// table. The editor preview never assigns an id.
struct LuaScriptInstance
{
    std::string scriptPath;
    bool        bLoaded                    = false;
    bool        bAuthoringPreviewAttempted = false;
    bool        bAuthoringPreviewLoaded    = false;
    /// Key in the runtime's live registry; 0 while not loaded by a runtime.
    uint64_t runtimeId = 0;

    std::vector<LuaScriptProperty>                       properties;
    std::unordered_map<std::string, script::ScriptValue> propertyOverrides;

    bool enabled = true;

    /// `executionOrder` declared by the script table; read on load, not serialized.
    int scriptExecutionOrder = 0;
    /// Per-instance override, serialized only when set.
    std::optional<int> executionOrderOverride;

    /// Lower runs first. Ties fall back to the host's own order.
    [[nodiscard]] int executionOrder() const { return executionOrderOverride.value_or(scriptExecutionOrder); }

    static YA_ECS_SYSTEMS_API std::string normalizeScriptPath(std::string_view path);
};

} // namespace ya
