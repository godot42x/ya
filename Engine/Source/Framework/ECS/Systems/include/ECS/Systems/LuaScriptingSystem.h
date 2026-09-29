#pragma once
#include "ECS/Systems/ScriptingSystem.h"
#include "ECS/Systems/LuaScriptInstance.h"
#include "Core/Input/InputManager.h"
#include "Core/Scripting/ScriptValue.h"
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

/// Narrow runtime services for the Lua bindings, injected by the Host. The
/// system never reaches Host/App types.
struct LuaRuntimeServices
{
    const InputManager*         input            = nullptr;
    std::function<bool()>       isMouseCaptured;   ///< Host input-router state
    std::function<double()>     elapsedSeconds;    ///< Seconds since app start
    std::function<uint64_t()>   frameIndex;        ///< App frame counter
    std::function<Scene*()>     activeScene;
    /// Script source by normalized path. Unset reads through the VFS.
    std::function<bool(const std::string& path, std::string& out)> readScript;
};

/// What a script instance is attached to. The system keeps one host per live
/// instance and never learns the host's type: an entity, a widget, anything
/// that can hand back its instance and put its own fields on `self`.
struct ILuaScriptHost
{
    virtual ~ILuaScriptHost() = default;

    /// The instance this host carries (`runtimeId == instanceId`), or null once
    /// the host or the instance is gone. Called on every operation, so an
    /// instance stored in movable storage may be looked up afresh.
    [[nodiscard]] virtual LuaScriptInstance* resolve(uint64_t instanceId) = 0;
    /// Write the host's fields on `self` (e.g. `self.entity`). Runs after the
    /// chunk loads and before every callback.
    virtual void bindSelf(sol::table& self) = 0;
};

enum class ELuaScriptCallback : uint8_t
{
    Init,
    Start,
    Update,
    Destroy,
};

/// The one Lua state of a running game. Loads, calls, hot-reloads and
/// destroys script instances for any host; entities of the active scene are
/// the host it drives itself (`onUpdate`). Owns the engine bindings and the
/// hot-reload watch.
struct YA_ECS_SYSTEMS_API LuaScriptingSystem : public ScriptingSystem
{
  private:
    LuaRuntimeServices _services;
    sol::state         _lua;
    /// Live instances by id. Ordered by id, i.e. load order, so reload and
    /// destroyAll are deterministic. Ids are never reused.
    std::map<uint64_t, std::unique_ptr<ILuaScriptHost>> _live;
    uint64_t _nextId           = 1;
    bool     _hotReloadEnabled = false;
    /// Scene-tree pre-order rank by entity index, for world-script order;
    /// rebuilt only when the active scene tree's revision moves.
    std::vector<uint32_t> _treeRanks;
    uint64_t              _rankedTreeRevision = 0;

  public:
    LuaScriptingSystem() = default;
    ~LuaScriptingSystem() override;
    LuaScriptingSystem(const LuaScriptingSystem&)            = delete;
    LuaScriptingSystem& operator=(const LuaScriptingSystem&) = delete;

    /// Injected seam (bound by the Host at startup; no App access from here).
    void setRuntimeServices(LuaRuntimeServices services);

    /// The shared state every host's scripts run in (bindings are added here).
    [[nodiscard]] sol::state& lua() { return _lua; }

    void init() override;
    /// One world-script frame. Instances are ordered by
    /// (executionOrder, scene-tree pre-order, index on the entity). Instances
    /// loaded this frame get onInit (all of them), then onStart (all of them),
    /// then every loaded instance gets onUpdate. Instances that appear while
    /// this runs join next frame.
    void onUpdate(float deltaTime) override;
    /// Play stopped: onDestroy every live instance, whatever hosts it, and
    /// reset the active scene's script rows for the next play.
    void onStop();
    /// `entity` is about to be destroyed: onDestroy its loaded scripts and
    /// drop their Lua handles.
    void onEntityDestroying(Entity& entity);

    // === Instances, any host ===
    /// Run `instance.scriptPath`, bind its callbacks, let `host` fill `self`,
    /// capture properties and apply overrides, then register the instance.
    /// Calls no lifecycle callback: the caller owns when onInit / onStart run.
    bool load(LuaScriptInstance& instance, std::unique_ptr<ILuaScriptHost> host);
    /// Call one lifecycle callback with `self` rebound by its host. Errors are
    /// logged and contained to this instance. False if the instance is not live.
    bool call(LuaScriptInstance& instance, ELuaScriptCallback callback, float deltaTime = 0.0f);
    /// Call `self:<callback>(args...)` if the script defines it (own field or
    /// class chain). True only when the callback returned `true`; a missing
    /// callback, an error or any other result is false. Errors stay contained.
    bool invoke(LuaScriptInstance& instance, const char* callback, const std::vector<sol::object>& args = {});
    /// Named call for script-to-script interaction (rpg-prototype R2a):
    /// `self:<name>(args...)` with the host's self binding refreshed first.
    /// Returns the first return value; nil when the script does not define
    /// `name` or the instance is not live. A failing target raises ScriptError,
    /// so the mistake surfaces at the caller instead of vanishing as a nil.
    [[nodiscard]] script::ScriptValue callNamed(LuaScriptInstance& instance, const std::string& name,
                                                script::ScriptArgs args);
    /// onDestroy, drop Lua handles and unregister. No-op if not live.
    void destroy(LuaScriptInstance& instance);
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
    /// Run `source` and bind the returned table onto `instance` (self and callbacks).
    bool bindChunk(LuaScriptInstance& instance, const std::string& source);
    /// Hot-reload one live instance from `source`: onDestroy, rebind, restore
    /// the saved property values, onInit, onStart. False when it did not
    /// survive its own onDestroy or the chunk failed to load.
    bool reloadInstance(ILuaScriptHost* host, uint64_t id, const std::string& source);
    [[nodiscard]] ILuaScriptHost* hostOf(const LuaScriptInstance& instance) const;
    [[nodiscard]] std::vector<uint64_t> liveIds() const;
    [[nodiscard]] const std::vector<uint32_t>& treeRanks(Scene& scene);
};

/// The entity face of callNamed, exported to scripts as `entity:call(name, ...)`
/// (rpg-prototype R2a). The first loaded script on `entity` that defines `name`
/// answers, with `self.entity` rebound first — what its host's bindSelf does.
/// Nil when no script defines the name; a failing target raises ScriptError.
[[nodiscard]] YA_ECS_SYSTEMS_API script::ScriptValue callEntityScript(Entity& entity, const std::string& name,
                                                                      script::ScriptArgs args);



} // namespace ya
