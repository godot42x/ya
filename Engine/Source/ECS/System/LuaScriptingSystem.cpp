#include "LuaScriptingSystem.h"
#include "Core/App/App.h"
#include "Core/Reflection/MetadataSupport.h"
#include "Core/System/VirtualFileSystem.h"
#include "Core/System/FileWatcher.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Scene/SceneManager.h"
#include <glm/glm.hpp>



namespace ya
{

void LuaScriptingSystem::init()
{
    YA_CORE_INFO("LuaScriptingSystem::init");

    _lua.set_exception_handler([](lua_State * /*L*/, sol::optional<const std::exception &> e, sol::string_view desc) {
        YA_CORE_ERROR("Lua Exception: {},  {}", e->what(), desc);
        return 0;
    });
    _lua.open_libraries(sol::lib::base,
                        sol::lib::package,
                        sol::lib::string,
                        sol::lib::math,
                        sol::lib::table,
                        sol::lib::os);

    // 设置全局环境标识
    _lua["IS_EDITOR"]  = false;
    _lua["IS_RUNTIME"] = true;

    // 配置 Lua 模块搜索路径（支持 require）
    // 添加 Engine/Content/Lua 和项目脚本目录到搜索路径
    _lua.script(R"(
        -- 添加引擎 Lua 库路径
        package.path = package.path .. ';./Engine/Content/Lua/?.lua'
        package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
        
        -- 添加项目脚本路径（相对于工作目录）
        package.path = package.path .. ';./Content/Scripts/?.lua'
        package.path = package.path .. ';./Content/Scripts/?/init.lua'
        
        print('[Lua] Package search paths configured:')
        print(package.path)
    )");

    // 暴露 glm::vec3 类型
    _lua.new_usertype<glm::vec3>("Vec3",
                                 sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                 "x",
                                 &glm::vec3::x,
                                 "y",
                                 &glm::vec3::y,
                                 "z",
                                 &glm::vec3::z,
                                 "__add",
                                 [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; });

    // ========================================================================
    // 高性能组件：手动绑定（避免反射开销）
    // ========================================================================

    // TransformComponent - 热点组件，使用原生绑定
    _lua.new_usertype<TransformComponent>("TransformComponent",
                                          sol::no_constructor,
                                          // 直接成员访问（零开销）
                                          "position",
                                          &TransformComponent::_position,
                                          "rotation",
                                          &TransformComponent::_rotation,
                                          "scale",
                                          &TransformComponent::_scale,
                                          // 方法绑定
                                          "getPosition",
                                          &TransformComponent::getPosition,
                                          "setPosition",
                                          &TransformComponent::setPosition,
                                          "getRotation",
                                          &TransformComponent::getRotation,
                                          "setRotation",
                                          &TransformComponent::setRotation,
                                          "getScale",
                                          &TransformComponent::getScale,
                                          "setScale",
                                          &TransformComponent::setScale);

    // 暴露 Entity (通用接口)
    _lua.new_usertype<Entity>(
        "Entity",
        "hasTransform",
        [](Entity &e) { return e.hasComponent<TransformComponent>(); },
        "getTransform",
        [](Entity &e) -> TransformComponent * {
            return e.hasComponent<TransformComponent>() ? e.getComponent<TransformComponent>() : nullptr;
        });

    // ========================================================================
    // 自动绑定所有反射组件（跳过已手动绑定的）
    // ========================================================================
    bindReflectedComponents();

    // 启用脚本热重载
    enableHotReload();
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    auto *scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp = view.get<LuaScriptComponent>(entityHandle);

        Entity entity(entityHandle, scene);

        // 遍历所有脚本实例
        for (auto &script : luaComp.scripts) {
            // 首次加载脚本
            if (!script.bLoaded && !script.scriptPath.empty()) {
                std::string scriptContent;
                if (VirtualFileSystem::get()->readFileToString(script.scriptPath, scriptContent)) {
                    try {
                        // 【重要】不再使用独立环境，改为共享全局环境
                        // 原因：
                        // 1. 支持 require() 导入公共模块
                        // 2. 脚本间可以共享工具库（如 Vector3 工具函数）
                        // 3. 减少内存开销
                        // 注意：脚本应该返回 local 表避免全局污染

                        sol::table scriptTable = _lua.script(scriptContent);

                        script.self      = scriptTable;
                        script.onInit    = scriptTable["onInit"];
                        script.onUpdate  = scriptTable["onUpdate"];
                        script.onDestroy = scriptTable["onDestroy"];
                        script.onEnable  = scriptTable["onEnable"];
                        script.onDisable = scriptTable["onDisable"];

                        // 设置 entity 引用
                        script.self["entity"] = &entity;

                        // 刷新属性列表并应用编辑器修改的覆盖值
                        script.refreshProperties();
                        script.applyPropertyOverrides(_lua);

                        // 调用 onInit
                        if (script.onInit.valid()) {
                            script.onInit(script.self);
                        }

                        script.bLoaded = true;
                        YA_CORE_INFO("Loaded Lua script: {}", script.scriptPath);
                    }
                    catch (const sol::error &e) {
                        YA_CORE_ERROR("Lua script error ({}): {}", script.scriptPath, e.what());
                    }
                    catch (const std::exception &e) {
                        YA_CORE_ERROR("Lua script error: {}", e.what());
                    }
                }
                else {
                    YA_CORE_ERROR("Failed to load Lua script: {}", script.scriptPath);
                }
            }

            // 调用 onUpdate（如果脚本已加载且启用）
            if (script.enabled && script.bLoaded && script.onUpdate.valid()) {
                try {
                    // 更新 entity 引用（防止 entity 被移动）
                    script.self["entity"] = &entity;

                    script.onUpdate(script.self, deltaTime);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("Lua onUpdate error ({}): {}", script.scriptPath, e.what());
                }
            }
        }
    }
}

void LuaScriptingSystem::onStop()
{
    // TODO: let app use serialization to reload all/ recreate entity and components
    auto *scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp = view.get<LuaScriptComponent>(entityHandle);

        // 调用所有脚本的 onDestroy
        for (auto &script : luaComp.scripts) {
            if (script.bLoaded && script.onDestroy.valid()) {
                try {
                    script.onDestroy(script.self);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("Lua onDestroy error ({}): {}", script.scriptPath, e.what());
                }
            }
            script.bLoaded = false;
        }
    }
}
// ============================================================================
// 通用组件绑定 - 利用反射 visitor 自动绑定所有属性
// ============================================================================

// template <typename ComponentType>
// void LuaScriptingSystem::bindComponentAuto(const std::string &className)
// {
//     using namespace ya::reflection;
// TODO: unimplemented
// How to get static type so that can transfer property value between sol::object and std::any?

// // 验证组件是否已注册反射（通过尝试获取属性列表）
// auto *cls = ClassRegistry::instance().getClass(className);
// if (!cls || cls->properties.empty()) {
//     YA_CORE_WARN("No reflection properties found for: {}", className);
//     return;
// }

// _lua.new_usertype<ComponentType>(
//     className,
//     sol::no_constructor,
//     // 反射属性绑定
//     "__index",
//     [cls](ComponentType &self, const std::string &key) -> sol::object {
//         auto *prop = cls->getProperty(key);
//         }
//     },
//     "__newindex",
//     [cls](ComponentType &self, const std::string &key, sol::object value) {
//         auto *prop = cls->getProperty(key);
//         if (prop) {
//             reflection::setPropertyValueFromSolObject(self, *prop, value);
//         }
//     });

//     YA_CORE_TRACE("  Auto-bound component: {}", className);
// }

void LuaScriptingSystem::bindReflectedComponents()
{
    YA_CORE_INFO("Auto-binding reflected components to Lua...");

    // bindComponentAuto<PointLightComponent>("PointLightComponent");
    // bindComponentAuto<CameraComponent>("CameraComponent");
    // TODO: 实现  UFUNCTION
    // bindComponentAuto<TransformComponent>("TransformComponent");
}

void LuaScriptingSystem::reloadScript(const std::string &scriptPath)
{
    YA_CORE_INFO("[Hot Reload] Reloading script: {}", scriptPath);

    auto *scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    // 查找所有使用该脚本的实体
    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto  &luaComp = view.get<LuaScriptComponent>(entityHandle);
        Entity entity(entityHandle, scene);

        for (auto &script : luaComp.scripts) {
            if (script.scriptPath != scriptPath) continue;

            // 保存当前属性值
            std::unordered_map<std::string, sol::object> savedProperties;
            if (script.self.valid()) {
                for (const auto &prop : script.properties) {
                    savedProperties[prop.name] = script.self[prop.name];
                }
            }

            // 调用 onDestroy（如果存在）
            if (script.onDestroy.valid()) {
                try {
                    script.onDestroy(script.self);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("[Hot Reload] onDestroy error: {}", e.what());
                }
            }

            // 重新加载脚本
            std::string scriptContent;
            if (VirtualFileSystem::get()->readFileToString(scriptPath, scriptContent)) {
                try {
                    sol::table scriptTable = _lua.script(scriptContent);

                    script.self      = scriptTable;
                    script.onInit    = scriptTable["onInit"];
                    script.onUpdate  = scriptTable["onUpdate"];
                    script.onDestroy = scriptTable["onDestroy"];
                    script.onEnable  = scriptTable["onEnable"];
                    script.onDisable = scriptTable["onDisable"];

                    // 设置 entity 引用
                    script.self["entity"] = &entity;

                    // 刷新属性并恢复值
                    script.refreshProperties();
                    for (const auto &[propName, value] : savedProperties) {
                        if (value.valid()) {
                            script.self[propName] = value;
                        }
                    }

                    // 应用编辑器覆盖值
                    script.applyPropertyOverrides(_lua);

                    // 调用 onInit
                    if (script.onInit.valid()) {
                        script.onInit(script.self);
                    }

                    YA_CORE_INFO("[Hot Reload] Successfully reloaded: {}", scriptPath);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("[Hot Reload] Failed to reload {}: {}", scriptPath, e.what());
                }
            }
        }
    }
}

void LuaScriptingSystem::enableHotReload()
{
    if (_hotReloadEnabled) return;

    auto *watcher = FileWatcher::get();
    if (!watcher) {
        YA_CORE_WARN("FileWatcher not initialized, hot reload disabled");
        return;
    }

    // 监视 Lua 脚本目录
    watcher->watchDirectory("Engine/Content/Lua", ".lua", [this](const FileWatcher::FileEvent &event) {
        if (event.type == FileWatcher::ChangeType::Modified) {
            reloadScript(event.path);
        }
    });

    watcher->watchDirectory("Content/Scripts", ".lua", [this](const FileWatcher::FileEvent &event) {
        if (event.type == FileWatcher::ChangeType::Modified) {
            reloadScript(event.path);
        }
    });

    _hotReloadEnabled = true;
    YA_CORE_INFO("[Hot Reload] Enabled for Lua scripts");
}

void LuaScriptingSystem::disableHotReload()
{
    if (!_hotReloadEnabled) return;

    auto *watcher = FileWatcher::get();
    if (watcher) {
        watcher->unwatchDirectory("Engine/Content/Lua");
        watcher->unwatchDirectory("Content/Scripts");
    }

    _hotReloadEnabled = false;
    YA_CORE_INFO("[Hot Reload] Disabled");
}

} // namespace ya
