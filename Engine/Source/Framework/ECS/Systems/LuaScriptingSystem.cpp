#include "ECS/Systems/LuaScriptingSystem.h"
#include "ECS/Systems/LuaEvent.h"
#include "ECS/Systems/LuaScriptBinding.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Core/GameMounts.h"
#include "Core/System/FileWatcher.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Entity.h"
#include <glm/glm.hpp>

#include <algorithm>
#include <functional>
#include <iterator>
#include <optional>
#include <unordered_map>
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

/// Calls a Lua function behind sol::protected_function: a script error comes
/// back as a failed result instead of unwinding through sol's call frames.
template <typename... Args>
sol::protected_function_result protectedCall(const sol::function& callback, Args&&... args)
{
    lua_State* L = callback.lua_state();
    callback.push(L);
    sol::protected_function protectedCallback(L, -1);
    lua_pop(L, 1);
    return protectedCallback(std::forward<Args>(args)...);
}

/// Runs `callback` if the script defines it; a failed call is logged with
/// `what` / `scriptPath` and contained to this instance.
template <typename... Args>
void invokeLuaCallback(const sol::function& callback, std::string_view what, std::string_view scriptPath, Args&&... args)
{
    if (!callback.valid()) {
        return;
    }
    const sol::protected_function_result result = protectedCall(callback, std::forward<Args>(args)...);
    if (!result.valid()) {
        const sol::error error = result;
        YA_CORE_ERROR("Lua {} error ({}): {}", what, scriptPath, error.what());
    }
}

void bindScriptTable(sol::state_view lua, ya::LuaScriptInstance& script, sol::table scriptTable)
{
    script.self      = scriptTable;
    script.listeners = ya::LuaListenerScope::bind(scriptTable, script.scriptPath);
    script.onInit    = functionField(lua, scriptTable, "onInit");
    script.onStart   = functionField(lua, scriptTable, "onStart");
    script.onUpdate  = functionField(lua, scriptTable, "onUpdate");
    script.onDestroy = functionField(lua, scriptTable, "onDestroy");
    script.onEnable  = functionField(lua, scriptTable, "onEnable");
    script.onDisable = functionField(lua, scriptTable, "onDisable");
    script.scriptExecutionOrder = scriptTable.get<sol::optional<int>>("executionOrder").value_or(0);
}

/// The script with `id` on `handle`, or null. Scripts live in
/// `LuaScriptComponent::scripts`, which moves with the registry's storage and
/// with the vector, so they are found by id each time rather than by address.
ya::LuaScriptInstance* findScript(ya::Scene& scene, entt::entity handle, uint64_t id)
{
    entt::registry& registry = scene.getRegistry();
    if (id == 0 || !registry.valid(handle)) {
        return nullptr;
    }
    auto* component = registry.try_get<ya::LuaScriptComponent>(handle);
    if (!component) {
        return nullptr;
    }
    for (auto& script : component->scripts) {
        if (script.runtimeId == id) {
            return &script;
        }
    }
    return nullptr;
}

/// A script on an entity of the active scene. The scene is asked for on every
/// use instead of kept: a scene unloaded while its scripts are live leaves the
/// host resolving nothing (ids are never reused), never a dangling scene.
struct FEntityScriptHost final : ya::ILuaScriptHost
{
    const std::function<ya::Scene*()>* activeScene;
    entt::entity                       handle;

    FEntityScriptHost(const std::function<ya::Scene*()>* inActiveScene, entt::entity inHandle)
        : activeScene(inActiveScene), handle(inHandle) {}

    [[nodiscard]] ya::Scene* scene() const { return *activeScene ? (*activeScene)() : nullptr; }

    ya::LuaScriptInstance* resolve(uint64_t instanceId) override
    {
        ya::Scene* current = scene();
        return current ? findScript(*current, handle, instanceId) : nullptr;
    }

    void bindSelf(sol::table& self) override
    {
        ya::Scene*        current = scene();
        const ya::Entity* entity  = current ? current->getEntityByEnttID(handle) : nullptr;
        if (entity) {
            self["entity"] = ya::LuaScriptObject{ya::script::entityRef(entity)};
        }
        else {
            self["entity"] = sol::lua_nil;
        }
    }
};

struct FScriptSlot
{
    entt::entity handle;
    size_t       index;
    uint64_t     id;
    int          order;
    uint32_t     treeRank;
    bool         bFresh;
};

bool runsBefore(const FScriptSlot& a, const FScriptSlot& b)
{
    if (a.order != b.order) {
        return a.order < b.order;
    }
    if (a.treeRank != b.treeRank) {
        return a.treeRank < b.treeRank;
    }
    if (a.handle != b.handle) {
        return entt::to_integral(a.handle) < entt::to_integral(b.handle);
    }
    return a.index < b.index;
}

/// Loaded world scripts of `scene` in execution order. `treeRanks` is the
/// scene-tree pre-order rank by entity index (0 when unranked). `loadPending`
/// gets each unloaded row that has a path and returns whether it loaded it
/// just now.
std::vector<FScriptSlot> collectWorldSlots(ya::Scene&                                                       scene,
                                           const std::vector<uint32_t>&                                     treeRanks,
                                           const std::function<bool(entt::entity, ya::LuaScriptInstance&)>& loadPending)
{
    entt::registry& registry = scene.getRegistry();
    // Snapshot the handles first: loading runs script chunks, which may add
    // entities or scripts, and the storage must not change under a live view.
    std::vector<entt::entity> handles;
    {
        auto view = registry.view<ya::LuaScriptComponent>();
        handles.assign(view.begin(), view.end());
    }

    std::vector<FScriptSlot> slots;
    for (const entt::entity handle : handles) {
        if (!scene.getEntityByEnttID(handle) || !registry.all_of<ya::LuaScriptComponent>(handle)) {
            continue;
        }
        const auto     entityIndex = static_cast<size_t>(entt::to_entity(handle));
        const uint32_t treeRank    = entityIndex < treeRanks.size() ? treeRanks[entityIndex] : 0;
        for (size_t index = 0;; ++index) {
            // Re-fetch every step: a loading chunk can grow this vector.
            auto& scripts = registry.get<ya::LuaScriptComponent>(handle).scripts;
            if (index >= scripts.size()) {
                break;
            }
            auto& script = scripts[index];
            bool  bFresh = false;
            if (!script.bLoaded && !script.scriptPath.empty() && loadPending) {
                script.scriptPath = ya::LuaScriptInstance::normalizeScriptPath(script.scriptPath);
                bFresh            = loadPending(handle, script);
            }
            const auto& loaded = registry.get<ya::LuaScriptComponent>(handle).scripts[index];
            if (loaded.bLoaded) {
                slots.push_back(FScriptSlot{
                    .handle = handle,
                    .index  = index,
                    .id     = loaded.runtimeId,
                    .order    = loaded.executionOrder(),
                    .treeRank = treeRank,
                    .bFresh   = bFresh,
                });
            }
        }
    }
    std::stable_sort(slots.begin(), slots.end(), runsBefore);
    return slots;
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
    sol::state& lua = _lua;

    lua.set_exception_handler([](lua_State * /*L*/, sol::optional<const std::exception &> e, sol::string_view desc) {
        YA_CORE_ERROR("Lua Exception: {},  {}", e->what(), desc);
        return 0;
    });
    lua.open_libraries(sol::lib::base,
                        sol::lib::package,
                        sol::lib::string,
                        sol::lib::math,
                        sol::lib::table,
                        sol::lib::os);
    LuaListenerScope::registerTypes(lua);

    // 设置全局环境标识
    lua["IS_EDITOR"]  = false;
    lua["IS_RUNTIME"] = true;

    std::string projectScriptRoot;
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (const auto contentRoot = vfs ? vfs->getMountPoint(game::mounts::Content) : std::nullopt; contentRoot.has_value()) {
        projectScriptRoot = (*contentRoot / "Scripts").lexically_normal().string();
        std::replace(projectScriptRoot.begin(), projectScriptRoot.end(), '\\', '/');
    }
    lua["YA_PROJECT_SCRIPT_ROOT"] = projectScriptRoot;

    // 配置 Lua 模块搜索路径（支持 require）
    // 添加 Engine/Content/Lua 和项目脚本目录到搜索路径
    lua.script(R"(
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

    registerLuaScriptBindings(lua);

    lua.new_usertype<LuaInputApi>(
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
        auto ekey = lua.create_named_table("EKey");
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
        auto emouse = lua.create_named_table("EMouse");
        emouse["Left"] = EMouse::Left;
        emouse["Middle"] = EMouse::Middle;
        emouse["Right"] = EMouse::Right;
        emouse["X1"] = EMouse::X1;
        emouse["X2"] = EMouse::X2;
    }

    lua.new_usertype<LuaTimeApi>(
        "Time",
        sol::no_constructor,
        "getElapsedSeconds",
        &LuaTimeApi::getElapsedSeconds,
        "getFrameIndex",
        &LuaTimeApi::getFrameIndex);

    lua.new_usertype<LuaLogApi>(
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

    lua.new_enum("CameraProjection",
                  "Perspective",
                  ECameraProjection::Perspective,
                  "Orthographic",
                  ECameraProjection::Orthographic);

    lua["input"] = LuaInputApi{.input = _services.input, .isMouseCapturedFn = _services.isMouseCaptured};
    lua["time"]  = LuaTimeApi{.elapsedSeconds = _services.elapsedSeconds, .frameIndex = _services.frameIndex};
    lua["log"]   = LuaLogApi{};

    // 启用脚本热重载
    enableHotReload();
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    YA_PROFILE_FUNCTION();

    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;

    const std::vector<FScriptSlot> slots =
        collectWorldSlots(*scene, treeRanks(*scene), [this](entt::entity handle, LuaScriptInstance& script) {
            return load(script, std::make_unique<FEntityScriptHost>(&_services.activeScene, handle));
        });

    // A callback may destroy or reshape any slot, so each step resolves again.
    auto resolve = [&](const FScriptSlot& slot) { return findScript(*scene, slot.handle, slot.id); };

    for (const FScriptSlot& slot : slots) {
        if (!slot.bFresh) {
            continue;
        }
        if (auto* script = resolve(slot)) {
            call(*script, ELuaScriptCallback::Init);
        }
    }
    for (const FScriptSlot& slot : slots) {
        if (!slot.bFresh) {
            continue;
        }
        if (auto* script = resolve(slot)) {
            call(*script, ELuaScriptCallback::Start);
        }
    }
    for (const FScriptSlot& slot : slots) {
        auto* script = resolve(slot);
        if (!script || !script->enabled) {
            continue;
        }
        YA_PROFILE_SCOPE("LuaScriptingSystem::scriptOnUpdate");
        call(*script, ELuaScriptCallback::Update, deltaTime);
    }
}

const std::vector<uint32_t>& LuaScriptingSystem::treeRanks(Scene& scene)
{
    const Node* root = scene.getRootNode();
    if (!root || root->getTreeRevision() == _rankedTreeRevision) {
        return _treeRanks;
    }
    _rankedTreeRevision = root->getTreeRevision();
    _treeRanks.clear();

    uint32_t                 rank = 0;
    std::vector<const Node*> stack{root};
    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();
        if (const Entity* entity = node->getEntity()) {
            const auto index = static_cast<size_t>(entt::to_entity(entity->getHandle()));
            if (index >= _treeRanks.size()) {
                _treeRanks.resize(index + 1, 0);
            }
            _treeRanks[index] = rank;
        }
        ++rank;
        const auto& children = node->getChildren();
        stack.insert(stack.end(), children.rbegin(), children.rend());
    }
    return _treeRanks;
}

void LuaScriptingSystem::onEntityDestroying(Entity& entity)
{
    auto* luaComp = entity.hasComponent<LuaScriptComponent>() ? entity.getComponent<LuaScriptComponent>() : nullptr;
    if (!luaComp) {
        return;
    }
    std::vector<uint64_t> ids;
    for (const auto& script : luaComp->scripts) {
        if (script.runtimeId != 0) {
            ids.push_back(script.runtimeId);
        }
    }
    Scene* scene = entity.getScene();
    if (!scene) {
        return;
    }
    for (const uint64_t id : ids) {
        if (LuaScriptInstance* script = findScript(*scene, entity.getHandle(), id)) {
            destroy(*script);
        }
    }
}

void LuaScriptingSystem::onStop()
{
    YA_PROFILE_FUNCTION();

    destroyAll();

    // TODO: let app use serialization to reload all/ recreate entity and components
    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;

    auto view = scene->getRegistry().view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        for (auto &script : view.get<LuaScriptComponent>(entityHandle).scripts) {
            script.bLoaded = false;
            script.bAuthoringPreviewAttempted = false;
            script.bAuthoringPreviewLoaded = false;
            script.properties.clear();
            script.releaseLuaHandles();
        }
    }
}

LuaScriptingSystem::~LuaScriptingSystem()
{
    // Hosts must destroy their instances while their storage is alive (play
    // stop, entity destroy). Anything left here may point at freed storage, so
    // it is only dropped, never resolved.
    if (!_live.empty()) {
        YA_CORE_WARN("LuaScriptingSystem: {} script instance(s) still live at shutdown", _live.size());
    }
    _live.clear();
}

bool LuaScriptingSystem::readSource(const std::string& path, std::string& out) const
{
    if (_services.readScript) {
        return _services.readScript(path, out);
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    return vfs && vfs->readFileToString(path, out);
}

bool LuaScriptingSystem::bindChunk(LuaScriptInstance& instance, const std::string& source)
{
    try {
        // Shared global environment so require() and common helper modules
        // work; a script returns a local table to avoid polluting globals.
        sol::table scriptTable = _lua.script(source);
        bindScriptTable(_lua, instance, scriptTable);
        return true;
    }
    catch (const sol::error& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", instance.scriptPath, e.what());
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", instance.scriptPath, e.what());
    }
    return false;
}

ILuaScriptHost* LuaScriptingSystem::hostOf(const LuaScriptInstance& instance) const
{
    if (instance.runtimeId == 0) {
        return nullptr;
    }
    auto it = _live.find(instance.runtimeId);
    return it != _live.end() ? it->second.get() : nullptr;
}

std::vector<uint64_t> LuaScriptingSystem::liveIds() const
{
    std::vector<uint64_t> ids;
    ids.reserve(_live.size());
    for (const auto& [id, host] : _live) {
        ids.push_back(id);
    }
    return ids;
}

bool LuaScriptingSystem::load(LuaScriptInstance& instance, std::unique_ptr<ILuaScriptHost> host)
{
    YA_PROFILE_SCOPE("LuaScriptingSystem::loadScript");
    if (!host || instance.scriptPath.empty()) {
        return false;
    }
    if (hostOf(instance)) {
        YA_CORE_WARN("LuaScriptingSystem: {} is already loaded", instance.scriptPath);
        return false;
    }
    std::string source;
    if (!readSource(instance.scriptPath, source)) {
        YA_CORE_ERROR("Failed to load Lua script: {}", instance.scriptPath);
        return false;
    }
    if (!bindChunk(instance, source)) {
        instance.releaseLuaHandles();
        return false;
    }
    host->bindSelf(instance.self);
    instance.refreshProperties();
    instance.applyPropertyOverrides(_lua);
    instance.runtimeId = _nextId++;
    instance.bLoaded   = true;
    _live.emplace(instance.runtimeId, std::move(host));
    YA_CORE_INFO("Loaded Lua script: {}", instance.scriptPath);
    return true;
}

bool LuaScriptingSystem::call(LuaScriptInstance& instance, ELuaScriptCallback callback, float deltaTime)
{
    ILuaScriptHost* host = hostOf(instance);
    if (!host || !instance.bLoaded) {
        return false;
    }
    host->bindSelf(instance.self);
    // The callback may move the instance's storage (a script adding a script
    // to its own entity), so nothing below reads `instance` after the call.
    const std::string path = instance.scriptPath;
    switch (callback) {
    case ELuaScriptCallback::Init:
        invokeLuaCallback(instance.onInit, "onInit", path, instance.self);
        break;
    case ELuaScriptCallback::Start:
        invokeLuaCallback(instance.onStart, "onStart", path, instance.self);
        break;
    case ELuaScriptCallback::Update:
        invokeLuaCallback(instance.onUpdate, "onUpdate", path, instance.self, deltaTime);
        break;
    case ELuaScriptCallback::Destroy:
        invokeLuaCallback(instance.onDestroy, "onDestroy", path, instance.self);
        break;
    }
    return true;
}

bool LuaScriptingSystem::invoke(LuaScriptInstance& instance, const char* callback, const std::vector<sol::object>& args)
{
    ILuaScriptHost* host = hostOf(instance);
    if (!host || !instance.bLoaded) {
        return false;
    }
    host->bindSelf(instance.self);
    const sol::function function = functionField(_lua, instance.self, callback);
    if (!function.valid()) {
        return false;
    }
    const std::string path = instance.scriptPath;
    const sol::table  self = instance.self;
    // The callback may move the instance's storage, so nothing below reads
    // `instance` after the call.
    const sol::protected_function_result result = protectedCall(function, self, sol::as_args(args));
    if (!result.valid()) {
        const sol::error error = result;
        YA_CORE_ERROR("Lua {} error ({}): {}", callback, path, error.what());
        return false;
    }
    if (result.return_count() == 0) {
        return false;
    }
    const sol::object value = result.get<sol::object>();
    return value.is<bool>() && value.as<bool>();
}

void LuaScriptingSystem::destroy(LuaScriptInstance& instance)
{
    const uint64_t id   = instance.runtimeId;
    auto           node = id != 0 ? _live.extract(id) : decltype(_live)::node_type{};
    if (node.empty()) {
        return;
    }
    // Unregistered before onDestroy runs, so a callback that destroys the same
    // instance again is a no-op instead of a recursion.
    std::unique_ptr<ILuaScriptHost> host = std::move(node.mapped());
    if (instance.bLoaded) {
        host->bindSelf(instance.self);
        const std::string   path      = instance.scriptPath;
        const sol::function onDestroy = instance.onDestroy;
        const sol::table    self      = instance.self;
        invokeLuaCallback(onDestroy, "onDestroy", path, self);
    }
    if (LuaScriptInstance* current = host->resolve(id)) {
        current->releaseLuaHandles();
        current->bLoaded   = false;
        current->runtimeId = 0;
    }
}

void LuaScriptingSystem::destroyAll()
{
    for (const uint64_t id : liveIds()) {
        auto it = _live.find(id);
        if (it == _live.end()) {
            continue;
        }
        if (LuaScriptInstance* instance = it->second->resolve(id)) {
            destroy(*instance);
        }
        else {
            _live.erase(it);
        }
    }
}

bool LuaScriptingSystem::reloadInstance(ILuaScriptHost* host, uint64_t id, const std::string& source)
{
    LuaScriptInstance* instance = host->resolve(id);
    if (!instance) {
        return false;
    }

    std::unordered_map<std::string, sol::object> savedProperties;
    for (const auto& prop : instance->properties) {
        savedProperties[prop.name] = instance->self[prop.name];
    }
    call(*instance, ELuaScriptCallback::Destroy);

    // Callbacks may have destroyed the instance or moved its storage.
    instance = host->resolve(id);
    if (!instance || !bindChunk(*instance, source)) {
        return false;
    }
    host->bindSelf(instance->self);
    instance->refreshProperties();
    for (const auto& [name, value] : savedProperties) {
        if (value.valid()) {
            instance->self[name] = value;
        }
    }
    instance->applyPropertyOverrides(_lua);

    // A reloaded instance is a batch of one: onInit, then onStart.
    call(*instance, ELuaScriptCallback::Init);
    if (LuaScriptInstance* started = host->resolve(id)) {
        call(*started, ELuaScriptCallback::Start);
    }
    return true;
}

void LuaScriptingSystem::reloadScript(const std::string& scriptPath)
{
    const std::string path = LuaScriptInstance::normalizeScriptPath(scriptPath);
    YA_CORE_INFO("[Hot Reload] Reloading script: {}", path);

    std::string source;
    bool        bSourceRead = false;
    for (const uint64_t id : liveIds()) {
        auto it = _live.find(id);
        if (it == _live.end()) {
            continue;
        }
        ILuaScriptHost*    host     = it->second.get();
        LuaScriptInstance* instance = host->resolve(id);
        if (!instance) {
            _live.erase(it);
            continue;
        }
        if (LuaScriptInstance::normalizeScriptPath(instance->scriptPath) != path) {
            continue;
        }
        if (!bSourceRead) {
            if (!readSource(path, source)) {
                YA_CORE_ERROR("[Hot Reload] Failed to read {}", path);
                return;
            }
            bSourceRead = true;
        }
        if (reloadInstance(host, id, source)) {
            YA_CORE_INFO("[Hot Reload] Successfully reloaded: {}", path);
        }
    }
}

size_t LuaScriptingSystem::liveCount()
{
    for (auto it = _live.begin(); it != _live.end();) {
        it = it->second->resolve(it->first) ? std::next(it) : _live.erase(it);
    }
    return _live.size();
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
