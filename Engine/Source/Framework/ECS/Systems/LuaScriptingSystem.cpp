#include "ECS/Systems/LuaScriptingSystem.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Reflection/MetadataSupport.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Core/GameMounts.h"
#include "Core/System/FileWatcher.h"
#include "Scene/Core/Scene.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Entity.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{

struct LuaInputApi
{
    const ya::InputManager*      input = nullptr;
    std::function<bool()> isMouseCapturedFn;

    [[nodiscard]] bool isKeyDown(ya::EKey::T key) const { return input && input->isKeyPressed(key); }
    [[nodiscard]] bool isKeyPressed(ya::EKey::T key) const { return input && input->wasKeyPressed(key); }
    [[nodiscard]] bool isKeyReleased(ya::EKey::T key) const { return input && input->wasKeyReleased(key); }
    [[nodiscard]] bool isMouseButtonDown(ya::EMouse::T button) const { return input && input->isMouseButtonPressed(button); }
    [[nodiscard]] bool isMouseButtonPressed(ya::EMouse::T button) const { return input && input->wasMouseButtonPressed(button); }
    [[nodiscard]] bool isMouseButtonReleased(ya::EMouse::T button) const { return input && input->wasMouseButtonReleased(button); }
    [[nodiscard]] glm::vec2 getMousePosition() const { return input ? input->getMousePosition() : glm::vec2(0.0f); }
    [[nodiscard]] glm::vec2 getMouseDelta() const { return input ? input->getMouseDelta() : glm::vec2(0.0f); }
    [[nodiscard]] glm::vec2 getMouseScrollDelta() const { return input ? input->getMouseScrollDelta() : glm::vec2(0.0f); }
    [[nodiscard]] bool isActionDown(const std::string& action) const { return input && input->isActionPressed(action); }
    [[nodiscard]] bool wasActionPressed(const std::string& action) const { return input && input->wasActionPressed(action); }
    [[nodiscard]] bool wasActionReleased(const std::string& action) const { return input && input->wasActionReleased(action); }

    [[nodiscard]] bool isMouseCaptured() const { return isMouseCapturedFn ? isMouseCapturedFn() : false; }
};

struct LuaTimeApi
{
    std::function<double()>   elapsedSeconds;
    std::function<uint64_t()> frameIndex;

    [[nodiscard]] double getElapsedSeconds() const
    {
        return elapsedSeconds ? elapsedSeconds() : 0.0;
    }

    [[nodiscard]] uint64_t getFrameIndex() const
    {
        return frameIndex ? frameIndex() : 0;
    }
};

struct LuaLogApi
{
    void info(const std::string& message) const { YA_INFO("{}", message); }
    void warn(const std::string& message) const { YA_WARN("{}", message); }
    void error(const std::string& message) const { YA_ERROR("{}", message); }
    void debug(const std::string& message) const { YA_DEBUG("{}", message); }
};

// table["name"] goes through sol's proxy assignment, which does not keep the
// instance function (the call returns without entering it). Read the field
// off the stack and store that reference.
sol::function functionField(sol::state_view lua, const sol::table& table, const char* key)
{
    lua_State* L = lua.lua_state();
    table.push(L);
    lua_getfield(L, -1, key);
    sol::function function;
    if (lua_isfunction(L, -1)) {
        function = sol::function(L, -1);
    }
    lua_pop(L, 2);
    return function;
}

template <typename... Args>
void invokeLuaCallback(const sol::function& callback,
                       std::string_view     what,
                       std::string_view     scriptPath,
                       Args&&... args)
{
    if (!callback.valid()) {
        return;
    }
    lua_State* L = callback.lua_state();
    callback.push(L);
    sol::protected_function protectedCallback(L, -1);
    lua_pop(L, 1);
    const sol::protected_function_result result = protectedCallback(std::forward<Args>(args)...);
    if (!result.valid()) {
        const sol::error error = result;
        YA_CORE_ERROR("Lua {} error ({}): {}", what, scriptPath, error.what());
    }
}

void bindScriptTable(sol::state_view lua, ya::LuaScriptComponent::ScriptInstance& script, sol::table scriptTable)
{
    script.self      = scriptTable;
    script.onInit    = functionField(lua, scriptTable, "onInit");
    script.onStart   = functionField(lua, scriptTable, "onStart");
    script.onUpdate  = functionField(lua, scriptTable, "onUpdate");
    script.onDestroy = functionField(lua, scriptTable, "onDestroy");
    script.onEnable  = functionField(lua, scriptTable, "onEnable");
    script.onDisable = functionField(lua, scriptTable, "onDisable");
    script.scriptExecutionOrder = scriptTable.get<sol::optional<int>>("executionOrder").value_or(0);
}

/// Child indices from the scene root down to the entity's node. Comparing two
/// paths lexicographically is scene-tree pre-order (an ancestor's path is a
/// prefix of its descendants'). An entity without a node has an empty path.
std::vector<size_t> treePath(ya::Scene& scene, entt::entity handle)
{
    std::vector<size_t> path;
    for (ya::Node* node = scene.getNodeByEntity(handle); node && node->getParent(); node = node->getParent()) {
        path.push_back(node->getParent()->getChildIndex(node));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

struct FScriptSlot
{
    entt::entity        handle;
    size_t              index;
    int                 order;
    std::vector<size_t> path;
    bool                bFresh;
};

bool runsBefore(const FScriptSlot& a, const FScriptSlot& b)
{
    if (a.order != b.order) {
        return a.order < b.order;
    }
    if (a.path != b.path) {
        return std::lexicographical_compare(a.path.begin(), a.path.end(), b.path.begin(), b.path.end());
    }
    if (a.handle != b.handle) {
        return entt::to_integral(a.handle) < entt::to_integral(b.handle);
    }
    return a.index < b.index;
}

} // namespace



namespace ya
{

void LuaScriptingSystem::setRuntimeServices(LuaRuntimeServices services)
{
    _services = std::move(services);
}

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

    std::string projectScriptRoot;
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (const auto contentRoot = vfs ? vfs->getMountPoint(game::mounts::Content) : std::nullopt; contentRoot.has_value()) {
        projectScriptRoot = (*contentRoot / "Scripts").lexically_normal().string();
        std::replace(projectScriptRoot.begin(), projectScriptRoot.end(), '\\', '/');
    }
    _lua["YA_PROJECT_SCRIPT_ROOT"] = projectScriptRoot;

    // 配置 Lua 模块搜索路径（支持 require）
    // 添加 Engine/Content/Lua 和项目脚本目录到搜索路径
    _lua.script(R"(
        -- 添加引擎 Lua 库路径
        package.path = package.path .. ';./Engine/Content/Lua/?.lua'
        package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
        
        -- 添加项目脚本路径（相对于工作目录）
        package.path = package.path .. ';./Content/Scripts/?.lua'
        package.path = package.path .. ';./Content/Scripts/?/init.lua'

        -- 添加当前项目脚本路径（打包后/非工作区路径）
        if YA_PROJECT_SCRIPT_ROOT ~= nil and YA_PROJECT_SCRIPT_ROOT ~= '' then
            package.path = package.path .. ';' .. YA_PROJECT_SCRIPT_ROOT .. '/?.lua'
            package.path = package.path .. ';' .. YA_PROJECT_SCRIPT_ROOT .. '/?/init.lua'
        end
        
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
                                 [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; },
                                 "__sub",
                                 [](const glm::vec3 &a, const glm::vec3 &b) { return a - b; },
                                 "__mul",
                                 sol::overload([](const glm::vec3 &v, float s) { return v * s; },
                                               [](float s, const glm::vec3 &v) { return s * v; }),
                                 "__div",
                                 [](const glm::vec3 &v, float s) { return v / s; },
                                 "length",
                                 [](const glm::vec3 &v) { return glm::length(v); },
                                 "normalize",
                                 [](const glm::vec3 &v) { return glm::normalize(v); },
                                 "dot",
                                 [](const glm::vec3 &a, const glm::vec3 &b) { return glm::dot(a, b); },
                                 "cross",
                                 [](const glm::vec3 &a, const glm::vec3 &b) { return glm::cross(a, b); });

    _lua.new_usertype<glm::vec2>("Vec2",
                                 sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
                                 "x",
                                 &glm::vec2::x,
                                 "y",
                                 &glm::vec2::y,
                                 "__add",
                                 [](const glm::vec2 &a, const glm::vec2 &b) { return a + b; },
                                 "__sub",
                                 [](const glm::vec2 &a, const glm::vec2 &b) { return a - b; },
                                 "__mul",
                                 sol::overload([](const glm::vec2 &v, float s) { return v * s; },
                                               [](float s, const glm::vec2 &v) { return s * v; }),
                                 "__div",
                                 [](const glm::vec2 &v, float s) { return v / s; },
                                 "length",
                                 [](const glm::vec2 &v) { return glm::length(v); },
                                 "normalize",
                                 [](const glm::vec2 &v) { return glm::normalize(v); });

    _lua.new_usertype<LuaInputApi>(
        "Input",
        sol::no_constructor,
        "isKeyDown",
        &LuaInputApi::isKeyDown,
        "isKeyPressed",
        &LuaInputApi::isKeyPressed,
        "isKeyReleased",
        &LuaInputApi::isKeyReleased,
        "isMouseButtonDown",
        &LuaInputApi::isMouseButtonDown,
        "isMouseButtonPressed",
        &LuaInputApi::isMouseButtonPressed,
        "isMouseButtonReleased",
        &LuaInputApi::isMouseButtonReleased,
        "getMousePosition",
        &LuaInputApi::getMousePosition,
        "getMouseDelta",
        &LuaInputApi::getMouseDelta,
        "getMouseScrollDelta",
        &LuaInputApi::getMouseScrollDelta,
        "isActionDown",
        &LuaInputApi::isActionDown,
        "wasActionPressed",
        &LuaInputApi::wasActionPressed,
        "wasActionReleased",
        &LuaInputApi::wasActionReleased,
        "isMouseCaptured",
        &LuaInputApi::isMouseCaptured);

    // EKey enum: expose key constants to Lua as a table
    {
        auto ekey = _lua.create_named_table("EKey");
        ekey["K_A"] = EKey::K_A;  ekey["K_B"] = EKey::K_B;  ekey["K_C"] = EKey::K_C;
        ekey["K_D"] = EKey::K_D;  ekey["K_E"] = EKey::K_E;  ekey["K_F"] = EKey::K_F;
        ekey["K_G"] = EKey::K_G;  ekey["K_H"] = EKey::K_H;  ekey["K_I"] = EKey::K_I;
        ekey["K_J"] = EKey::K_J;  ekey["K_K"] = EKey::K_K;  ekey["K_L"] = EKey::K_L;
        ekey["K_M"] = EKey::K_M;  ekey["K_N"] = EKey::K_N;  ekey["K_O"] = EKey::K_O;
        ekey["K_P"] = EKey::K_P;  ekey["K_Q"] = EKey::K_Q;  ekey["K_R"] = EKey::K_R;
        ekey["K_S"] = EKey::K_S;  ekey["K_T"] = EKey::K_T;  ekey["K_U"] = EKey::K_U;
        ekey["K_V"] = EKey::K_V;  ekey["K_W"] = EKey::K_W;  ekey["K_X"] = EKey::K_X;
        ekey["K_Y"] = EKey::K_Y;  ekey["K_Z"] = EKey::K_Z;
        ekey["K_0"] = EKey::K_0;  ekey["K_1"] = EKey::K_1;  ekey["K_2"] = EKey::K_2;
        ekey["K_3"] = EKey::K_3;  ekey["K_4"] = EKey::K_4;  ekey["K_5"] = EKey::K_5;
        ekey["K_6"] = EKey::K_6;  ekey["K_7"] = EKey::K_7;  ekey["K_8"] = EKey::K_8;
        ekey["K_9"] = EKey::K_9;
        ekey["K_GRAVE"] = EKey::K_GRAVE; // ` / ~ (toggle mouse capture)
        ekey["Space"] = EKey::Space;      ekey["Escape"] = EKey::Escape;
        ekey["Enter"] = EKey::Enter;      ekey["Tab"] = EKey::Tab;
        ekey["Backspace"] = EKey::Backspace;
        ekey["LShift"] = EKey::LShift;    ekey["RShift"] = EKey::RShift;
        ekey["LCtrl"] = EKey::LCtrl;      ekey["RCtrl"] = EKey::RCtrl;
        ekey["LAlt"] = EKey::LAlt;        ekey["RAlt"] = EKey::RAlt;
        ekey["Up"] = EKey::Up;            ekey["Down"] = EKey::Down;
        ekey["Left"] = EKey::Left;        ekey["Right"] = EKey::Right;
        ekey["F1"] = EKey::F1;   ekey["F2"] = EKey::F2;   ekey["F3"] = EKey::F3;
        ekey["F4"] = EKey::F4;   ekey["F5"] = EKey::F5;   ekey["F6"] = EKey::F6;
        ekey["F7"] = EKey::F7;   ekey["F8"] = EKey::F8;   ekey["F9"] = EKey::F9;
        ekey["F10"] = EKey::F10; ekey["F11"] = EKey::F11; ekey["F12"] = EKey::F12;
    }

    // EMouse enum: expose mouse button constants to Lua as a table
    {
        auto emouse = _lua.create_named_table("EMouse");
        emouse["Left"] = EMouse::Left;
        emouse["Middle"] = EMouse::Middle;
        emouse["Right"] = EMouse::Right;
        emouse["X1"] = EMouse::X1;
        emouse["X2"] = EMouse::X2;
    }

    _lua.new_usertype<LuaTimeApi>(
        "Time",
        sol::no_constructor,
        "getElapsedSeconds",
        &LuaTimeApi::getElapsedSeconds,
        "getFrameIndex",
        &LuaTimeApi::getFrameIndex);

    _lua.new_usertype<LuaLogApi>(
        "Log",
        sol::no_constructor,
        "info",
        &LuaLogApi::info,
        "warn",
        &LuaLogApi::warn,
        "error",
        &LuaLogApi::error,
        "debug",
        &LuaLogApi::debug);

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
                                          &TransformComponent::setScale,
                                          // Direction vectors (computed from rotation euler angles)
                                          "getForward",
                                          [](TransformComponent& t) -> glm::vec3 {
                                              glm::quat q = glm::quat(glm::radians(t._rotation));
                                              return q * glm::vec3(0.0f, 0.0f, -1.0f); // WorldForward
                                          },
                                          "getRight",
                                          [](TransformComponent& t) -> glm::vec3 {
                                              glm::quat q = glm::quat(glm::radians(t._rotation));
                                              return q * glm::vec3(1.0f, 0.0f, 0.0f); // WorldRight
                                          },
                                          "getUp",
                                          [](TransformComponent& t) -> glm::vec3 {
                                              glm::quat q = glm::quat(glm::radians(t._rotation));
                                              return q * glm::vec3(0.0f, 1.0f, 0.0f); // WorldUp
                                          });

    _lua.new_enum("CameraProjection",
                  "Perspective",
                  ECameraProjection::Perspective,
                  "Orthographic",
                  ECameraProjection::Orthographic);

    _lua.new_usertype<CameraComponent>("CameraComponent",
                                       sol::no_constructor,
                                       "primary",
                                       &CameraComponent::bPrimary,
                                       "fixedAspectRatio",
                                       &CameraComponent::_fixedAspectRatio,
                                       "projection",
                                       &CameraComponent::_projection,
                                       "fov",
                                       &CameraComponent::_fov,
                                       "orthoHalfHeight",
                                       &CameraComponent::_orthoHalfHeight,
                                       "aspectRatio",
                                       &CameraComponent::_aspectRatio,
                                       "nearClip",
                                       &CameraComponent::_nearClip,
                                       "farClip",
                                       &CameraComponent::_farClip,
                                       "distance",
                                       &CameraComponent::_distance,
                                       "focusPoint",
                                       &CameraComponent::_focusPoint,
                                       "setAspectRatio",
                                       &CameraComponent::setAspectRatio);

    // 暴露 Entity (通用接口)
    _lua.new_usertype<Entity>(
        "Entity",
        "hasTransform",
        [](Entity &e) { return e.hasComponent<TransformComponent>(); },
        "getTransform",
        [](Entity &e) -> TransformComponent * {
            return e.hasComponent<TransformComponent>() ? e.getComponent<TransformComponent>() : nullptr;
        },
        "hasCamera",
        [](Entity &e) { return e.hasComponent<CameraComponent>(); },
        "getCamera",
        [](Entity &e) -> CameraComponent * {
            return e.hasComponent<CameraComponent>() ? e.getComponent<CameraComponent>() : nullptr;
        });

    _lua["input"] = LuaInputApi{.input = _services.input, .isMouseCapturedFn = _services.isMouseCaptured};
    _lua["time"]  = LuaTimeApi{.elapsedSeconds = _services.elapsedSeconds, .frameIndex = _services.frameIndex};
    _lua["log"]   = LuaLogApi{};

    // ========================================================================
    // 自动绑定所有反射组件（跳过已手动绑定的）
    // ========================================================================
    bindReflectedComponents();

    // 启用脚本热重载
    enableHotReload();
}

bool LuaScriptingSystem::readScriptSource(const std::string& path, std::string& out) const
{
    if (_services.readScript) {
        return _services.readScript(path, out);
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    return vfs && vfs->readFileToString(path, out);
}

bool LuaScriptingSystem::loadInstance(LuaScriptComponent::ScriptInstance& script, Entity& entity)
{
    YA_PROFILE_SCOPE("LuaScriptingSystem::loadScript");
    std::string scriptContent;
    if (!readScriptSource(script.scriptPath, scriptContent)) {
        YA_CORE_ERROR("Failed to load Lua script: {}", script.scriptPath);
        return false;
    }
    try {
        // Shared global environment so require() and common helper modules
        // work; a script returns a local table to avoid polluting globals.
        sol::table scriptTable = _lua.script(scriptContent);
        bindScriptTable(_lua, script, scriptTable);
        script.self["entity"] = &entity;
        script.refreshProperties();
        script.applyPropertyOverrides(_lua);
        script.bLoaded = true;
        YA_CORE_INFO("Loaded Lua script: {}", script.scriptPath);
        return true;
    }
    catch (const sol::error& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", script.scriptPath, e.what());
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", script.scriptPath, e.what());
    }
    return false;
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    YA_PROFILE_FUNCTION();

    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;
    entt::registry& registry = scene->getRegistry();

    // Snapshot the handles first: loading runs script chunks, which may add
    // entities or scripts, and the storage must not change under a live view.
    std::vector<entt::entity> handles;
    {
        auto view = registry.view<LuaScriptComponent>();
        handles.assign(view.begin(), view.end());
    }

    std::vector<FScriptSlot> slots;
    for (const entt::entity handle : handles) {
        Entity* entity = scene->getEntityByEnttID(handle);
        if (!entity || !registry.all_of<LuaScriptComponent>(handle)) {
            continue;
        }
        const std::vector<size_t> path = treePath(*scene, handle);
        for (size_t index = 0;; ++index) {
            // Re-fetch every step: a loading chunk can grow this vector.
            auto& scripts = registry.get<LuaScriptComponent>(handle).scripts;
            if (index >= scripts.size()) {
                break;
            }
            auto& script = scripts[index];
            if (!script.scriptPath.empty()) {
                script.scriptPath = LuaScriptComponent::ScriptInstance::normalizeScriptPath(script.scriptPath);
            }
            bool bFresh = false;
            if (!script.bLoaded && !script.scriptPath.empty()) {
                bFresh = loadInstance(script, *entity);
            }
            if (registry.get<LuaScriptComponent>(handle).scripts[index].bLoaded) {
                slots.push_back(FScriptSlot{
                    .handle = handle,
                    .index  = index,
                    .order  = registry.get<LuaScriptComponent>(handle).scripts[index].executionOrder(),
                    .path   = path,
                    .bFresh = bFresh,
                });
            }
        }
    }
    std::stable_sort(slots.begin(), slots.end(), runsBefore);

    // A callback may destroy or reshape any slot, so each step resolves again.
    auto resolve = [&](const FScriptSlot& slot) -> LuaScriptComponent::ScriptInstance* {
        if (!registry.valid(slot.handle) || !registry.all_of<LuaScriptComponent>(slot.handle)) {
            return nullptr;
        }
        auto& scripts = registry.get<LuaScriptComponent>(slot.handle).scripts;
        if (slot.index >= scripts.size() || !scripts[slot.index].bLoaded) {
            return nullptr;
        }
        auto& script = scripts[slot.index];
        script.self["entity"] = scene->getEntityByEnttID(slot.handle);
        return &script;
    };

    for (const FScriptSlot& slot : slots) {
        if (!slot.bFresh) {
            continue;
        }
        if (auto* script = resolve(slot)) {
            invokeLuaCallback(script->onInit, "onInit", script->scriptPath, script->self);
        }
    }
    for (const FScriptSlot& slot : slots) {
        if (!slot.bFresh) {
            continue;
        }
        if (auto* script = resolve(slot)) {
            invokeLuaCallback(script->onStart, "onStart", script->scriptPath, script->self);
        }
    }
    for (const FScriptSlot& slot : slots) {
        auto* script = resolve(slot);
        if (!script || !script->enabled) {
            continue;
        }
        YA_PROFILE_SCOPE("LuaScriptingSystem::scriptOnUpdate");
        invokeLuaCallback(script->onUpdate, "onUpdate", script->scriptPath, script->self, deltaTime);
    }
}

void LuaScriptingSystem::onEntityDestroying(Entity& entity)
{
    auto* luaComp = entity.hasComponent<LuaScriptComponent>() ? entity.getComponent<LuaScriptComponent>() : nullptr;
    if (!luaComp) {
        return;
    }
    for (auto& script : luaComp->scripts) {
        if (script.bLoaded) {
            invokeLuaCallback(script.onDestroy, "onDestroy", script.scriptPath, script.self);
        }
        script.bLoaded = false;
        script.releaseLuaHandles();
    }
}

void LuaScriptingSystem::onStop()
{
    YA_PROFILE_FUNCTION();

    // TODO: let app use serialization to reload all/ recreate entity and components
    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto &luaComp = view.get<LuaScriptComponent>(entityHandle);

        // 调用所有脚本的 onDestroy
        for (auto &script : luaComp.scripts) {
            if (script.bLoaded && script.onDestroy.valid()) {
                try {
                    invokeLuaCallback(script.onDestroy, "onDestroy", script.scriptPath, script.self);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("Lua onDestroy error ({}): {}", script.scriptPath, e.what());
                }
            }
            script.bLoaded = false;
            script.bAuthoringPreviewAttempted = false;
            script.bAuthoringPreviewLoaded = false;
            script.properties.clear();
            script.releaseLuaHandles();
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
    const auto normalizedScriptPath = LuaScriptComponent::ScriptInstance::normalizeScriptPath(scriptPath);
    YA_CORE_INFO("[Hot Reload] Reloading script: {}", normalizedScriptPath);

    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;

    // 查找所有使用该脚本的实体
    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto  &luaComp = view.get<LuaScriptComponent>(entityHandle);
        Entity* entity = scene->getEntityByEnttID(entityHandle);
        if (!entity) {
            YA_CORE_WARN("LuaScriptingSystem: Entity {} no longer exists in scene during reload",
                         static_cast<uint32_t>(entityHandle));
            continue;
        }

        for (auto &script : luaComp.scripts) {
            script.scriptPath = LuaScriptComponent::ScriptInstance::normalizeScriptPath(script.scriptPath);
            // An instance that has not loaded yet reads the new source on its
            // first load; reloading it here would run onInit twice.
            if (script.scriptPath != normalizedScriptPath || !script.bLoaded) continue;

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
                    invokeLuaCallback(script.onDestroy, "onDestroy", script.scriptPath, script.self);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("[Hot Reload] onDestroy error: {}", e.what());
                }
            }

            // 重新加载脚本
            std::string scriptContent;
            if (readScriptSource(normalizedScriptPath, scriptContent)) {
                try {
                    sol::table scriptTable = _lua.script(scriptContent);
                    bindScriptTable(_lua, script, scriptTable);

                    // 设置 entity 引用
                    script.self["entity"] = entity;

                    // 刷新属性并恢复值
                    script.refreshProperties();
                    for (const auto &[propName, value] : savedProperties) {
                        if (value.valid()) {
                            script.self[propName] = value;
                        }
                    }

                    // 应用编辑器覆盖值
                    script.applyPropertyOverrides(_lua);

                    // A reloaded instance is a batch of one: onInit, then onStart.
                    invokeLuaCallback(script.onInit, "onInit", script.scriptPath, script.self);
                    invokeLuaCallback(script.onStart, "onStart", script.scriptPath, script.self);

                    YA_CORE_INFO("[Hot Reload] Successfully reloaded: {}", normalizedScriptPath);
                }
                catch (const sol::error &e) {
                    YA_CORE_ERROR("[Hot Reload] Failed to reload {}: {}", normalizedScriptPath, e.what());
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
