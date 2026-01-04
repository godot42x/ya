
#include "ScriptingSystem.h"
#include <functional>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>


namespace ya
{
struct LuaScriptingSystem : public ScriptingSystem
{

    sol::state _lua;
    // 热重载相关
    bool _hotReloadEnabled = false;

    std::unordered_set<std::string> _watchedScripts;

    void init() override;
    void onUpdate(float deltaTime) override;
    void onStop();

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
    // 自动绑定所有已注册的反射组件到Lua
    void bindReflectedComponents();

    // 通用组件绑定模板（利用反射自动绑定）
    // template <typename ComponentType>
    // void bindComponentAuto(const std::string &className);
};



} // namespace ya