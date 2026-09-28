#pragma once
#include "ECS/Systems/ScriptingSystem.h"
#include "ECS/Systems/LuaScriptInstance.h"
#include "Core/Input/InputManager.h"
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
    [[nodiscard]] ILuaScriptHost* hostOf(const LuaScriptInstance& instance) const;
    [[nodiscard]] std::vector<uint64_t> liveIds() const;

    // 自动绑定所有已注册的反射组件到Lua
    void bindReflectedComponents();

    // 通用组件绑定模板（利用反射自动绑定）
    // template <typename ComponentType>
    // void bindComponentAuto(const std::string &className);
};



} // namespace ya
