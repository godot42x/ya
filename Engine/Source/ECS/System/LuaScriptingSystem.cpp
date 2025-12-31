#include "LuaScriptingSystem.h"
#include "Core/App/App.h"
#include "Core/System/FileSystem.h"
#include "Core/System/ReflectionSystem.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Scene/SceneManager.h"
#include <glm/glm.hpp>

// Reflects
#include <reflects-core/lib.h>


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
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    auto *scene = App::get()->getSceneManager()->getActiveScene();
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
    auto *scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp   = view.get<LuaScriptComponent>(entityHandle);
        luaComp.bLoaded = false;
    }
}

void LuaScriptingSystem::bindReflectedComponents()
{
    YA_CORE_INFO("Auto-binding reflected components to Lua...");

    auto &registry = ClassRegistry::instance();

    // 已手动优化绑定的组件列表（跳过反射）
    static const std::unordered_set<std::string> manuallyBound = {
        "TransformComponent" // 热点组件，已手动绑定
        // 在这里添加其他需要性能优化的组件
    };

    // 遍历所有已注册的类
    for (const auto &[className, classInfo] : registry.classes) {
        // 只绑定组件类型（可以通过命名约定或基类判断）
        if (className.find("Component") != std::string::npos) {
            // 跳过已手动绑定的组件
            if (manuallyBound.count(className) > 0) {
                YA_CORE_TRACE("  Skipped (manually bound): {}", className);
                continue;
            }

            bindComponentType(classInfo.get());
            YA_CORE_TRACE("  Bound component: {}", className);
        }
    }
}

void LuaScriptingSystem::bindComponentType(Class *classInfo)
{
    if (!classInfo) return;

    const std::string &className = classInfo->_name;

    // 创建 Lua usertype 并设置 __index 和 __newindex 元方法
    sol::usertype<void> componentType = _lua.new_usertype<void>(
        className,
        sol::no_constructor, // 组件不应该在Lua中直接构造

        // __index 元方法：支持直接属性访问 component.position
        sol::meta_function::index,
        [this, classInfo](void *obj, const std::string &key) -> sol::object {
            auto it = classInfo->properties.find(key);
            if (it == classInfo->properties.end()) {
                YA_CORE_WARN("Property '{}' not found in {}", key, classInfo->_name);
                return sol::nil;
            }

            const Property &prop = it->second;
            if (!prop.getter) {
                YA_CORE_WARN("Property '{}' in {} has no getter", key, classInfo->_name);
                return sol::nil;
            }

            try {
                std::any value = prop.getter(obj);

                // 根据类型ID转换为Lua对象
                if (prop.typeIndex == refl::TypeIndex<glm::vec3>::value()) {
                    return sol::make_object(sol::state_view(_lua), std::any_cast<glm::vec3>(value));
                }
                else if (prop.typeIndex == refl::TypeIndex<float>::value()) {
                    return sol::make_object(sol::state_view(_lua), std::any_cast<float>(value));
                }
                else if (prop.typeIndex == refl::TypeIndex<int>::value()) {
                    return sol::make_object(sol::state_view(_lua), std::any_cast<int>(value));
                }
                else if (prop.typeIndex == refl::TypeIndex<bool>::value()) {
                    return sol::make_object(sol::state_view(_lua), std::any_cast<bool>(value));
                }
                else if (prop.typeIndex == refl::TypeIndex<std::string>::value()) {
                    return sol::make_object(sol::state_view(_lua), std::any_cast<std::string>(value));
                }
                // TODO: 添加更多类型支持
            }
            catch (const std::exception &e) {
                YA_CORE_ERROR("Lua __index error for {}.{}: {}", classInfo->_name, key, e.what());
            }
            return sol::nil;
        },

        // __newindex 元方法：支持直接属性赋值 component.position = vec3
        sol::meta_function::new_index,
        [this, classInfo](void *obj, const std::string &key, sol::object value) {
            auto it = classInfo->properties.find(key);
            if (it == classInfo->properties.end()) {
                YA_CORE_WARN("Property '{}' not found in {}", key, classInfo->_name);
                return;
            }

            const Property &prop = it->second;
            if (!prop.setter) {
                YA_CORE_WARN("Property '{}' in {} is read-only", key, classInfo->_name);
                return;
            }

            try {
                // 根据类型ID转换Lua对象到C++类型
                if (prop.typeIndex == refl::TypeIndex<glm::vec3>::value()) {
                    if (value.is<glm::vec3>()) {
                        prop.setter(obj, value.as<glm::vec3>());
                    }
                }
                else if (prop.typeIndex == refl::TypeIndex<float>::value()) {
                    if (value.is<float>()) {
                        prop.setter(obj, value.as<float>());
                    }
                }
                else if (prop.typeIndex == refl::TypeIndex<int>::value()) {
                    if (value.is<int>()) {
                        prop.setter(obj, value.as<int>());
                    }
                }
                else if (prop.typeIndex == refl::TypeIndex<bool>::value()) {
                    if (value.is<bool>()) {
                        prop.setter(obj, value.as<bool>());
                    }
                }
                else if (prop.typeIndex == refl::TypeIndex<std::string>::value()) {
                    if (value.is<std::string>()) {
                        prop.setter(obj, value.as<std::string>());
                    }
                }
            }
            catch (const std::exception &e) {
                YA_CORE_ERROR("Lua __newindex error for {}.{}: {}", classInfo->_name, key, e.what());
            }
        });

    // 仍然保留 getter/setter 方法作为备选
    for (const auto &[propName, prop] : classInfo->properties) {
        if (prop.bStatic) continue; // 跳过静态属性

        // Getter 方法
        if (prop.getter) {
            std::string getterName = "get" + std::string(1, std::toupper(propName[0])) + propName.substr(1);

            componentType[getterName] = [this, prop, classInfo, propName](void *obj) -> sol::object {
                try {
                    std::any value = prop.getter(obj);

                    // 根据类型ID转换为Lua对象
                    if (prop.typeIndex == refl::TypeIndex<glm::vec3>::value()) {
                        return sol::make_object(sol::state_view(_lua), std::any_cast<glm::vec3>(value));
                    }
                    else if (prop.typeIndex == refl::TypeIndex<float>::value()) {
                        return sol::make_object(sol::state_view(_lua), std::any_cast<float>(value));
                    }
                    else if (prop.typeIndex == ya::TypeIndex<int>::value()) {
                        return sol::make_object(sol::state_view{_lua}, std::any_cast<int>(value));
                    }
                    else if (prop.typeIndex == refl::TypeIndex<bool>::value()) {
                        return sol::make_object(sol::state_view(_lua), std::any_cast<bool>(value));
                    }
                    // TODO: 添加更多类型支持
                }
                catch (const std::exception &e) {
                    YA_CORE_ERROR("Lua getter error for {}.{}: {}", classInfo->_name, propName, e.what());
                }
                return sol::nil;
            };
        }

        // Setter (如果不是const)
        if (prop.setter && !prop.bConst) {
            std::string setterName = "set" + std::string(1, std::toupper(propName[0])) + propName.substr(1);

            componentType[setterName] = [prop, classInfo, propName](void *obj, sol::object value) {
                try {
                    // 根据类型ID转换Lua对象到C++类型
                    if (prop.typeIndex == refl::TypeIndex<glm::vec3>::value()) {
                        if (value.is<glm::vec3>()) {
                            prop.setter(obj, value.as<glm::vec3>());
                        }
                    }
                    else if (prop.typeIndex == refl::TypeIndex<float>::value()) {
                        if (value.is<float>()) {
                            prop.setter(obj, value.as<float>());
                        }
                    }
                    else if (prop.typeIndex == refl::TypeIndex<int>::value()) {
                        if (value.is<int>()) {
                            prop.setter(obj, value.as<int>());
                        }
                    }
                    else if (prop.typeIndex == refl::TypeIndex<bool>::value()) {
                        if (value.is<bool>()) {
                            prop.setter(obj, value.as<bool>());
                        }
                    }
                }
                catch (const std::exception &e) {
                    YA_CORE_ERROR("Lua setter error for {}.{}: {}", classInfo->_name, propName, e.what());
                }
            };
        }
    }

    // TODO: 自动绑定成员函数
    for (const auto &[funcName, func] : classInfo->functions) {
        // 函数绑定需要根据参数类型动态生成wrapper
        // 这需要更复杂的模板元编程或代码生成
    }
}

} // namespace ya
