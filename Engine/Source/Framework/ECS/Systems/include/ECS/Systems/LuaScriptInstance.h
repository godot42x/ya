#pragma once

#include "Core/Api.h"

#include <any>
#include <cstdint>
#include <optional>
#include <sol/sol.hpp>
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
    std::string name;
    std::any    value;
    std::string typeHint;
    float       min             = 0.0f;
    float       max             = 100.0f;
    std::string tooltip         = "";
    std::string serializedValue = "";
};

/// One script attached to one host. The authored half (path, enabled, order
/// override, property overrides) is what hosts serialize; the rest is live
/// state owned by whichever Lua state loaded it (`LuaScriptingSystem` for play,
/// the editor preview for property rows).
struct LuaScriptInstance
{
    std::string scriptPath;
    bool        bLoaded                    = false;
    bool        bAuthoringPreviewAttempted = false;
    bool        bAuthoringPreviewLoaded    = false;
    /// Key in the runtime's live registry; 0 while not loaded by a runtime.
    uint64_t    runtimeId = 0;

    sol::table self;

    sol::function onInit;
    sol::function onStart;
    sol::function onUpdate;
    sol::function onDestroy;
    sol::function onEnable;
    sol::function onDisable;

    std::vector<LuaScriptProperty>            properties;
    std::unordered_map<std::string, std::any> propertyOverrides;

    bool enabled = true;

    /// `executionOrder` declared by the script table; read on load, not serialized.
    int                scriptExecutionOrder = 0;
    /// Per-instance override, serialized only when set.
    std::optional<int> executionOrderOverride;

    /// Lower runs first. Ties fall back to the host's own order.
    [[nodiscard]] int executionOrder() const { return executionOrderOverride.value_or(scriptExecutionOrder); }

    YA_ECS_SYSTEMS_API void refreshProperties();
    YA_ECS_SYSTEMS_API void capturePropertiesFrom(sol::table table);
    YA_ECS_SYSTEMS_API void applyPropertyOverrides(sol::state& lua);
    YA_ECS_SYSTEMS_API void applyPropertyOverridesTo(sol::table table, sol::state& lua);
    /// Drop sol handles while their lua_State is still alive. Keeps path,
    /// enabled, propertyOverrides, and captured C++ property rows.
    YA_ECS_SYSTEMS_API void releaseLuaHandles();

    static YA_ECS_SYSTEMS_API std::string normalizeScriptPath(std::string_view path);
};

} // namespace ya
