#pragma once
#include "ECS/Systems/ScriptInstance.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "Core/Input/InputManager.h"
#include "Core/Scripting/ScriptValue.h"
#include "Script/Lua/ScriptInstanceOps.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <vector>


namespace ya
{

struct Scene;
struct Entity;
struct LuaInstanceLive;
struct LuaInstanceStore;

enum class ELuaScriptCallback : uint8_t
{
    Init,
    Start,
    Update,
    Destroy,
};

/// What a named call does when the target function fails. Lifecycle
/// notifications swallow; script-to-script calls throw.
enum class ENamedCallError : uint8_t
{
    Throw,
    Swallow,
};

/// The one Lua state of a running game. Loads, calls, hot-reloads and
/// destroys script instances for any host; entities of the active scene are
/// the host it drives itself (`onUpdate`). Owns the engine bindings and the
/// hot-reload watch. `persistentState()` stays on this class: the neutral
/// interface only resets it.
struct YA_SCRIPT_LUA_API LuaScriptingSystem : public IWorldScriptRuntime
{
  private:
    WorldScriptServices _services;
    sol::state         _lua;
    /// Hosts by id. Ordered by id, i.e. load order, so reload and destroyAll
    /// are deterministic. Ids are never reused. Sol tables live in
    /// `_instances`, not in this map.
    std::map<uint64_t, std::unique_ptr<IScriptInstanceHost>> _live;
    bool _hotReloadEnabled = false;
    /// Scene-tree pre-order rank by entity index, for world-script order;
    /// rebuilt only when the active scene tree's revision moves.
    std::vector<uint32_t> _treeRanks;
    uint64_t              _rankedTreeRevision = 0;
    /// The `Persist` global's table; created on first use, replaced on a play
    /// session exit. A plain table so scripts store any Lua value.
    sol::table _persistent;
    /// Sol tables for this system's state. Declared after `_lua` so it is
    /// destroyed first, while the state is still alive. Defined in the cpp.
    std::unique_ptr<LuaInstanceStore> _instances;

  public:
    LuaScriptingSystem();
    ~LuaScriptingSystem() override;
    LuaScriptingSystem(const LuaScriptingSystem&)            = delete;
    LuaScriptingSystem& operator=(const LuaScriptingSystem&) = delete;

    /// Injected seam (bound by the Host at startup; no App access from here).
    void setServices(WorldScriptServices services) override;
    /// Same as `setServices`. Kept so existing tests keep their call.
    void setRuntimeServices(WorldScriptServices services);

    void shutdown() override;

    /// The shared state every host's scripts run in (bindings are added here).
    [[nodiscard]] sol::state& lua() { return _lua; }

    void init() override;
    /// One world-script frame. Instances are ordered by
    /// (executionOrder, scene-tree pre-order, index on the entity). Instances
    /// loaded this frame get onInit (all of them), then onStart (all of them),
    /// then every loaded instance gets onUpdate. Instances that appear while
    /// this runs join next frame.
    void onUpdate(float deltaTime) override;
    /// `name()` on each loaded world script, in execution order. True when one
    /// returns true. Does not load scripts that are not already live.
    bool callEachLoaded(const std::string& name);
    /// Play stopped: onDestroy every live instance, whatever hosts it, and
    /// reset the active scene's script rows for the next play.
    void onStop() override;
    /// The gameplay persistent table, bound as the `Persist` global (rpg R3):
    /// chest flags and the like. Scene transfers never touch it; a play
    /// session exit calls resetPersistentState for a fresh one.
    [[nodiscard]] sol::table persistentState();
    /// Swap in a fresh `Persist` table. The old table object stays alive as
    /// long as scripts hold it, but the global reads the new empty one.
    void resetPersistentState() override;
    /// `entity` is about to be destroyed: onDestroy its loaded scripts and
    /// drop their Lua handles.
    void onEntityDestroying(Entity& entity) override;

    // === Instances, any host ===
    /// Run `instance.scriptPath`, bind its callbacks, let `host` fill `self`,
    /// capture properties and apply overrides, then register the instance.
    /// Calls no lifecycle callback: the caller owns when onInit / onStart run.
    bool load(ScriptInstance& instance, std::unique_ptr<IScriptInstanceHost> host);
    /// Call one lifecycle callback with `self` rebound by its host. Errors are
    /// logged and contained to this instance. False if the instance is not live.
    bool call(ScriptInstance& instance, ELuaScriptCallback callback, float deltaTime = 0.0f);
    /// `self:<name>(args...)` with the host's self binding refreshed first.
    /// Returns the first return value; empty when the script does not define
    /// `name` or the instance is not live. `onError` is Throw (raise
    /// ScriptError) or Swallow (log and return empty). A boolean return is an
    /// ordinary value; callers that mean "consumed" check it themselves.
    [[nodiscard]] script::ScriptValue callNamed(ScriptInstance& instance, const std::string& name,
                                                script::ScriptArgs args, ENamedCallError onError);
    /// The live `self` of `runtimeId`, or an invalid table. This is the script's
    /// own table; callers that care about identity compare it with lua_rawequal.
    [[nodiscard]] sol::table selfOf(uint64_t runtimeId) const;
    /// Drop the live table and unregister `runtimeId` without running onDestroy.
    /// No-op for 0 or an id this system does not own.
    void dropInstance(uint64_t runtimeId) override;

    /// The system whose `lua_State` is `L`, or null for a state this module
    /// did not construct (the property-preview state, for one).
    [[nodiscard]] static LuaScriptingSystem* systemForState(lua_State* L);
    /// Runtime id when the value at `index` is a live `self` of this system.
    /// Any other table is 0, so property tables still cannot cross.
    [[nodiscard]] uint64_t runtimeIdOfSelf(lua_State* L, int index) const;
    /// Keep the function at `index` in this system's state. 0 if it is not one.
    [[nodiscard]] uint64_t holdFunction(lua_State* L, int index);
    static void pushLiveSelf(lua_State* L, uint64_t runtimeId);
    static void pushHeldFunction(lua_State* L, uint64_t id);
    static void retainFunction(uint64_t id);
    static void releaseFunction(uint64_t id);
    static void invokeFunction(uint64_t id, uint64_t selfRuntimeId, script::ScriptArgs args);
    /// onDestroy, drop the live table and unregister. No-op if not live.
    void destroy(ScriptInstance& instance);
    /// destroy() every live instance, in load order.
    void destroyAll();
    /// Hot reload: every live instance of `scriptPath`, whatever hosts it:
    /// onDestroy, rebind, restore property values, onInit, onStart.
    void reloadScript(const std::string& scriptPath);
    /// Live instances whose host still resolves them; drops the rest.
    [[nodiscard]] size_t liveCount();

    /**
     * @brief 启用脚本文件监视（自动热重载）
     */
    void enableHotReload();

    /**
     * @brief 禁用脚本文件监视
     */
    void disableHotReload();

  private:
    [[nodiscard]] bool readSource(const std::string& path, std::string& out) const;
    /// Run `source` and publish the returned table into the live side table.
    /// A first load allocates `runtimeId`. A reload replaces the table for the
    /// id the instance already has; a failed chunk leaves that table in place.
    bool bindChunk(ScriptInstance& instance, const std::string& source);
    /// Hot-reload one live instance from `source`: onDestroy, rebind, restore
    /// the saved property values, onInit, onStart. False when it did not
    /// survive its own onDestroy or the chunk failed to load.
    bool reloadInstance(IScriptInstanceHost* host, uint64_t id, const std::string& source);
    [[nodiscard]] IScriptInstanceHost* hostOf(const ScriptInstance& instance) const;

    friend std::shared_ptr<void> scriptInstanceListenerOwner(uint64_t runtimeId);
    friend bool                  retainInstanceFunction(uint64_t runtimeId, script::ScriptFnHandle fn);
    friend bool                  scriptFunctionAlive(script::ScriptFnHandle fn);
    [[nodiscard]] LuaInstanceLive* findLive(uint64_t runtimeId);
    [[nodiscard]] const LuaInstanceLive* findLive(uint64_t runtimeId) const;
    void eraseLive(uint64_t runtimeId);
    [[nodiscard]] std::vector<uint64_t> liveIds() const;
    [[nodiscard]] const std::vector<uint32_t>& treeRanks(Scene& scene);
};

/// The entity face of callNamed, exported to scripts as `entity:call(name, ...)`
/// (rpg-prototype R2a). The first loaded script on `entity` that defines `name`
/// answers, with `self.entity` rebound first — what its host's contributeSelf does.
/// Nil when no script defines the name; a failing target raises ScriptError.
[[nodiscard]] YA_SCRIPT_LUA_API script::ScriptValue callEntityScript(Entity& entity, const std::string& name,
                                                                      script::ScriptArgs args);

/// Copy `_PROPERTIES` from `table` into `instance.properties`. `table` is not retained.
YA_SCRIPT_LUA_API void captureScriptProperties(ScriptInstance& instance, sol::table table);
/// Write `instance.propertyOverrides` onto `table`.
YA_SCRIPT_LUA_API void applyScriptPropertyOverrides(const ScriptInstance& instance, sol::table table, sol::state& lua);
/// `dropInstance` on the system that owns `runtimeId`. Installed as the ECS
/// `setDropScriptInstance` hook. 0 and an unknown id are no-ops. Does not
/// run onDestroy.
YA_SCRIPT_LUA_API void dropLuaScriptLive(uint64_t runtimeId);



} // namespace ya
