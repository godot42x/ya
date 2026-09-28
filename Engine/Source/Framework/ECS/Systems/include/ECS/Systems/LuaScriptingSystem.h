#pragma once
#include "ECS/Systems/ScriptingSystem.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "Core/Input/InputManager.h"
#include <functional>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>


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

struct YA_ECS_SYSTEMS_API LuaScriptingSystem : public ScriptingSystem
{
    /// Injected seam (bound by the Host at startup; no App access from here).
    void setRuntimeServices(LuaRuntimeServices services);

    sol::state _lua;
    // 热重载相关
    bool _hotReloadEnabled = false;

    std::unordered_set<std::string> _watchedScripts;

    void init() override;
    /// One world-script frame. Instances are ordered by
    /// (executionOrder, scene-tree pre-order, index on the entity). Instances
    /// loaded this frame get onInit (all of them), then onStart (all of them),
    /// then every loaded instance gets onUpdate. Instances that appear while
    /// this runs join next frame.
    void onUpdate(float deltaTime) override;
    void onStop();
    /// `entity` is about to be destroyed: onDestroy its loaded scripts and
    /// drop their Lua handles.
    void onEntityDestroying(Entity& entity);

    /**
     * @brief 重新加载指定脚本（热重载）
     * @param scriptPath 脚本路径
     */
    void reloadScript(const std::string &scriptPath);

    /**
     * @brief 启用脚本文件监视（自动热重载）
     */
    void enableHotReload();

    /**
     * @brief 禁用脚本文件监视
     */
    void disableHotReload();

  private:
    LuaRuntimeServices _services;

    [[nodiscard]] bool readScriptSource(const std::string& path, std::string& out) const;
    /// Run the chunk and bind callbacks, self.entity, properties and the
    /// declared execution order. Does not call onInit.
    bool loadInstance(LuaScriptComponent::ScriptInstance& script, Entity& entity);

    // 自动绑定所有已注册的反射组件到Lua
    void bindReflectedComponents();

    // 通用组件绑定模板（利用反射自动绑定）
    // template <typename ComponentType>
    // void bindComponentAuto(const std::string &className);
};



} // namespace ya
