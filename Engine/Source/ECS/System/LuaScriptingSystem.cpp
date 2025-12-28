#include "LuaScriptingSystem.h"
#include "Core/App/App.h"
#include "Core/System/FileSystem.h"
#include "Core/System/ReflectionSystem.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Scene/SceneManager.h"
#include <glm/glm.hpp>


namespace ya
{

void LuaScriptingSystem::init()
{
    YA_CORE_INFO("LuaScriptingSystem::init");

    _lua.set_exception_handler([](lua_State *L, sol::optional<const std::exception &> e, sol::string_view desc) {
        YA_CORE_ERROR("Lua Exception: {},  {}", e->what(), desc);
        return 0;
    });
    _lua.open_libraries(sol::lib::base,
                        sol::lib::package,
                        sol::lib::string,
                        sol::lib::math,
                        sol::lib::table,
                        sol::lib::os);

    // 暴露 glm::vec3 类型
    _lua.new_usertype<glm::vec3>("vec3",
                                 sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                 "x",
                                 &glm::vec3::x,
                                 "y",
                                 &glm::vec3::y,
                                 "z",
                                 &glm::vec3::z);

    // ReflectionSystem

    // 暴露 TransformComponent
    _lua.new_usertype<TransformComponent>("TransformComponent",
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

    // 暴露 Entity
    _lua.new_usertype<Entity>(
        "Entity",
        "hasTransform",
        [](Entity &e) { return e.hasComponent<TransformComponent>(); },
        "getTransform",
        [](Entity &e) -> TransformComponent * { return e.hasComponent<TransformComponent>() ? e.getComponent<TransformComponent>() : nullptr; });
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    auto *scene = App::get()->getSceneManager()->getCurrentScene();
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp = view.get<LuaScriptComponent>(entityHandle);

        // 首次加载脚本
        if (!luaComp.bLoaded && !luaComp.scriptPath.empty()) {
            std::string scriptContent;
            if (FileSystem::get()->readFileToString(luaComp.scriptPath, scriptContent)) {
                try {
                    // 执行脚本并获取返回的 table
                    sol::table scriptTable = _lua.script(scriptContent);

                    luaComp.self      = scriptTable;
                    luaComp.onInit    = scriptTable["onInit"];
                    luaComp.onUpdate  = scriptTable["onUpdate"];
                    luaComp.onDestroy = scriptTable["onDestroy"];

                    // 创建 Entity 对象并设置到脚本的 self 中
                    Entity entity(entityHandle, scene);
                    luaComp.self["entity"] = &entity;

                    // 调用 onInit
                    if (luaComp.onInit.valid()) {
                        luaComp.onInit(luaComp.self);
                    }

                    luaComp.bLoaded = true;
                    YA_CORE_INFO("Loaded Lua script: {}", luaComp.scriptPath);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("Lua script error ({}): {}", luaComp.scriptPath, e.what());
                }
                catch (const std::exception &e) {
                    YA_CORE_ERROR("Lua script error: {}", e.what());
                }
            }
            else {
                YA_CORE_ERROR("Failed to load Lua script: {}", luaComp.scriptPath);
            }
        }

        // 调用 onUpdate
        if (luaComp.bLoaded && luaComp.onUpdate.valid()) {
            try {
                // 更新 entity 引用（防止 entity 被移动）
                Entity entity(entityHandle, scene);
                luaComp.self["entity"] = &entity;

                luaComp.onUpdate(luaComp.self, deltaTime);
            }
            catch (const sol::error &e) {
                YA_CORE_ERROR("Lua onUpdate error ({}): {}", luaComp.scriptPath, e.what());
            }
        }
    }
}

void LuaScriptingSystem::onStop()
{
    // TODO: let app use serialization to reload all/ recreate entity and components
    auto *scene = App::get()->getSceneManager()->getCurrentScene();
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp   = view.get<LuaScriptComponent>(entityHandle);
        luaComp.bLoaded = false;
    }
}

} // namespace ya
