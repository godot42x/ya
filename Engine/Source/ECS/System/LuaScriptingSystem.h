
#include "ScriptingSystem.h"
#include <sol/sol.hpp>
#include <string>
#include <unordered_set>
#include <any>


struct Class;

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

    // 为单个组件类型注册Lua绑定
    void bindComponentType(Class *classInfo);
};



} // namespace ya