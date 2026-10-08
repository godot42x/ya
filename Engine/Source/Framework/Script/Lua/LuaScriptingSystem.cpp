#include "Script/Lua/LuaScriptingSystem.h"
#include "Script/Lua/LuaScriptBinding.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Scripting/ScriptBindings.h"
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
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>
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
    [[nodiscard]] bool isActionPressed(const std::string& action) const { return input && input->wasActionPressed(action); }
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

/// The self binding an entity host applies before every callback. Entity-to-
/// entity calls reach the instance through its component, not its host, and
/// reapply the same line (callEntityScript below).
void rebindEntitySelf(sol::table& self, const ya::Entity* entity)
{
    if (entity) {
        self["entity"] = ya::LuaScriptObject{ya::script::entityRef(entity)};
    }
    else {
        self["entity"] = sol::lua_nil;
    }
}

/// `self:<name>(args...)` on a loaded instance whose function the caller has
/// already found. Args cross as script values; the first return value comes
/// back as one. Throw surfaces the target's mistake at the caller; Swallow
/// logs it and returns empty. `self` is taken by value: the call may drop the
/// side-table entry, and this copy keeps the table alive through the call.
ya::script::ScriptValue callDefinedFunction(ya::ScriptInstance& script, sol::table self, const std::string& name,
                                            ya::script::ScriptArgs args, ya::ENamedCallError onError)
{
    lua_State* L = self.lua_state();
    const sol::function function = functionField(sol::state_view(L), self, name.c_str());
    std::vector<sol::object> arguments;
    arguments.reserve(args.size());
    for (const ya::script::ScriptValue& value : args) {
        ya::pushLuaValue(L, value);
        arguments.emplace_back(L, -1);
        lua_pop(L, 1);
    }
    const std::string path = script.scriptPath;
    // The call may move the instance's storage, so nothing below reads
    // `script` again.
    const sol::protected_function_result result = protectedCall(function, self, sol::as_args(arguments));
    if (!result.valid()) {
        const sol::error error = result;
        switch (onError) {
        case ya::ENamedCallError::Swallow:
        {
            YA_CORE_ERROR("Lua {} error ({}): {}", name, path, error.what());
            return {};
        } break;
        case ya::ENamedCallError::Throw:
        {
            throw ya::script::ScriptError(std::format("call '{}' ({}): {}", name, path, error.what()));
        } break;
        }
    }
    if (result.return_count() == 0) {
        return {};
    }
    return ya::toScriptValue(L, result.stack_index());
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

int destroySelfMethod(lua_State* L)
{
    auto* fn = static_cast<ya::script::ScriptFunction*>(lua_touserdata(L, 1));
    std::destroy_at(fn);
    return 0;
}

int callSelfMethod(lua_State* L)
{
    int results = -1;
    try {
        const auto* fn = static_cast<const ya::script::ScriptFunction*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::vector<ya::script::ScriptValue> args;
        const int last = lua_gettop(L);
        for (int index = 2; index <= last; ++index) {
            args.push_back(ya::toScriptValue(L, index));
        }
        const ya::script::ScriptValue result = (*fn)(args);
        if ((result).isNull()) {
            results = 0;
        }
        else {
            results = ya::pushLuaValue(L, result);
        }
    }
    catch (const std::exception& e) {
        lua_pushstring(L, e.what());
    }
    return results >= 0 ? results : lua_error(L);
}

void installSelfMethod(lua_State* L, int table, const std::string& name, const ya::script::ScriptFunction& fn)
{
    lua_pushlstring(L, name.data(), name.size());
    const bool bAlready = lua_rawget(L, table) == LUA_TFUNCTION;
    lua_pop(L, 1);
    if (bAlready) {
        return;
    }
    void* memory = lua_newuserdata(L, sizeof(ya::script::ScriptFunction));
    new (memory) ya::script::ScriptFunction(fn);
    if (luaL_newmetatable(L, "ya.ScriptSelfMethod")) {
        lua_pushcfunction(L, &destroySelfMethod);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushcclosure(L, &callSelfMethod, 1);
    lua_pushlstring(L, name.data(), name.size());
    lua_insert(L, -2);
    lua_rawset(L, table);
}

void applyHostSelf(sol::table& self, ya::IScriptInstanceHost& host)
{
    ya::ScriptSelfBuilder builder;
    host.contributeSelf(builder);
    lua_State* L = self.lua_state();
    self.push(L);
    const int table = lua_absindex(L, -1);
    for (const ya::ScriptSelfBuilder::Field& field : builder.fields) {
        lua_pushlstring(L, field.name.data(), field.name.size());
        ya::pushLuaValue(L, field.value);
        lua_rawset(L, table);
    }
    for (const ya::ScriptSelfBuilder::Method& method : builder.methods) {
        installSelfMethod(L, table, method.name, method.fn);
    }
    lua_pop(L, 1);
}

/// Id → owning system, and the process-wide id counter.
///
/// Sol tables are not in this registry. Each LuaScriptingSystem keeps those
/// in `_instances`, destroyed while that system's lua_State is still alive.
/// `dropScriptInstance(runtimeId)` (component release, editor path change)
/// has no system pointer, and two systems must not reuse an id, so the id
/// space and the owner lookup live here: raw pointers only, no lua_State,
/// no static sol destructor.
struct LiveOwnerRegistry
{
    std::unordered_map<uint64_t, ya::LuaScriptingSystem*> byId;
    std::unordered_map<uint64_t, ya::LuaScriptingSystem*> fnOwner;
    std::unordered_map<lua_State*, ya::LuaScriptingSystem*> byState;
    uint64_t                                                nextId = 1;
    uint64_t                                                nextFn = 1;
};

LiveOwnerRegistry& liveOwners()
{
    static LiveOwnerRegistry registry;
    return registry;
}

/// The script with `id` on `handle`, or null. Scripts live in
/// `LuaScriptComponent::scripts`, which moves with the registry's storage and
/// with the vector, so they are found by id each time rather than by address.
ya::ScriptInstance* findScript(ya::Scene& scene, entt::entity handle, uint64_t id)
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
struct FEntityScriptHost final : ya::IScriptInstanceHost
{
    const std::function<ya::Scene*()>* activeScene;
    entt::entity                       handle;

    FEntityScriptHost(const std::function<ya::Scene*()>* inActiveScene, entt::entity inHandle)
        : activeScene(inActiveScene), handle(inHandle) {}

    [[nodiscard]] ya::Scene* scene() const { return *activeScene ? (*activeScene)() : nullptr; }

    ya::ScriptInstance* resolve(uint64_t instanceId) override
    {
        ya::Scene* current = scene();
        return current ? findScript(*current, handle, instanceId) : nullptr;
    }

    void contributeSelf(ya::ScriptSelfBuilder& out) override
    {
        ya::Scene*        current = scene();
        const ya::Entity* entity  = current ? current->getEntityByEnttID(handle) : nullptr;
        out.field("entity", entity ? ya::script::scriptMake(ya::script::entityRef(entity)) : ya::script::ScriptValue{});
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
                                           const std::function<bool(entt::entity, ya::ScriptInstance&)>& loadPending)
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
                script.scriptPath = ya::ScriptInstance::normalizeScriptPath(script.scriptPath);
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

/// Sol handles for one loaded instance. Not on the component: the component
/// moves with EnTT storage, and the editor preview state must not share it.
/// Owned by one LuaScriptingSystem (`_instances`), so the key is just the
/// process-wide runtimeId.
/// Listeners and timer callbacks retained for one self. Dropping it (destroy
/// or a hot reload binding a new table) releases those functions.
struct ListenerOwner
{
    std::vector<script::ScriptFnHandle> functions;

    ~ListenerOwner()
    {
        for (const script::ScriptFnHandle fn : functions) {
            LuaScriptingSystem::releaseFunction(fn.id);
        }
    }
};

struct LuaInstanceLive
{
    sol::table                     self;
    sol::function                  onInit;
    sol::function                  onStart;
    sol::function                  onUpdate;
    sol::function                  onDestroy;
    sol::function                  onEnable;
    sol::function                  onDisable;
    std::shared_ptr<ListenerOwner> listeners;
};

struct LuaHeldFunction
{
    sol::function fn;
    int32_t       refs = 0;
};

struct LuaInstanceStore
{
    /// Destroyed after `byId`, so a listener owner's release still sees the map.
    std::unordered_map<uint64_t, LuaHeldFunction> functions;
    std::map<uint64_t, LuaInstanceLive>           byId;
};

script::ScriptValue callEntityScript(Entity& entity, const std::string& name, script::ScriptArgs args)
{
    auto* component =
        entity.hasComponent<LuaScriptComponent>() ? entity.getComponent<LuaScriptComponent>() : nullptr;
    if (!component) {
        return {};
    }
    for (ScriptInstance& script : component->scripts) {
        const auto owner = liveOwners().byId.find(script.runtimeId);
        if (owner == liveOwners().byId.end() || !owner->second) {
            continue;
        }
        sol::table self = owner->second->selfOf(script.runtimeId);
        if (!script.bLoaded || !self.valid()) {
            continue;
        }
        lua_State* L = self.lua_state();
        if (!functionField(sol::state_view(L), self, name.c_str()).valid()) {
            continue;
        }
        rebindEntitySelf(self, &entity);
        return callDefinedFunction(script, self, name, args, ENamedCallError::Throw);
    }
    return {};
}

void registerEntityScriptCall()
{
    static bool once = false;
    if (once) {
        return;
    }
    Entity::__ensure_reflection_registered();
    if (ClassRegistry::instance().getClass(type_index_v<Entity>) == nullptr) {
        return;
    }
    once = true;
    script::addMethod(type_index_v<Entity>, "call", script::fnVariadic<Entity&, std::string>(
        [](Entity& entity, script::ScriptArgs args) -> script::ScriptValue {
            if (args.empty()) {
                throw script::ScriptError("call: expects the function name as its first argument");
            }
            const std::string name = script::scriptToString(args[0]);
            return callEntityScript(entity, name, args.subspan(1));
        }));
}

void LuaScriptingSystem::setServices(WorldScriptServices services)
{
    _services = std::move(services);
}

void LuaScriptingSystem::setRuntimeServices(WorldScriptServices services)
{
    setServices(std::move(services));
}

void LuaScriptingSystem::shutdown() {}

LuaScriptingSystem* LuaScriptingSystem::systemForState(lua_State* L)
{
    if (!L) {
        return nullptr;
    }
    const auto it = liveOwners().byState.find(L);
    return it == liveOwners().byState.end() ? nullptr : it->second;
}

uint64_t LuaScriptingSystem::runtimeIdOfSelf(lua_State* L, int index) const
{
    if (!L || !_instances || lua_type(L, index) != LUA_TTABLE) {
        return 0;
    }
    const int abs = lua_absindex(L, index);
    for (const auto& [id, live] : _instances->byId) {
        if (!live.self.valid() || live.self.lua_state() != L) {
            continue;
        }
        live.self.push(L);
        const bool same = lua_rawequal(L, abs, -1) != 0;
        lua_pop(L, 1);
        if (same) {
            return id;
        }
    }
    return 0;
}

uint64_t LuaScriptingSystem::holdFunction(lua_State* L, int index)
{
    if (!L || !_instances || L != _lua.lua_state() || lua_type(L, index) != LUA_TFUNCTION) {
        return 0;
    }
    sol::function fn(L, index);
    if (!fn.valid()) {
        return 0;
    }
    const uint64_t id = liveOwners().nextFn++;
    _instances->functions.emplace(id, LuaHeldFunction{.fn = std::move(fn), .refs = 0});
    liveOwners().fnOwner.emplace(id, this);
    return id;
}

void LuaScriptingSystem::pushLiveSelf(lua_State* L, uint64_t runtimeId)
{
    if (!L) {
        return;
    }
    if (runtimeId == 0) {
        lua_pushnil(L);
        return;
    }
    const auto owner = liveOwners().byId.find(runtimeId);
    if (owner == liveOwners().byId.end() || !owner->second) {
        lua_pushnil(L);
        return;
    }
    const sol::table self = owner->second->selfOf(runtimeId);
    if (!self.valid()) {
        lua_pushnil(L);
        return;
    }
    self.push(L);
}

void LuaScriptingSystem::pushHeldFunction(lua_State* L, uint64_t id)
{
    if (!L) {
        return;
    }
    const auto owner = liveOwners().fnOwner.find(id);
    if (owner == liveOwners().fnOwner.end() || !owner->second || !owner->second->_instances) {
        lua_pushnil(L);
        return;
    }
    const auto it = owner->second->_instances->functions.find(id);
    if (it == owner->second->_instances->functions.end() || !it->second.fn.valid() || it->second.fn.lua_state() != L) {
        lua_pushnil(L);
        return;
    }
    it->second.fn.push(L);
}

void LuaScriptingSystem::retainFunction(uint64_t id)
{
    const auto owner = liveOwners().fnOwner.find(id);
    if (owner == liveOwners().fnOwner.end() || !owner->second || !owner->second->_instances) {
        return;
    }
    const auto it = owner->second->_instances->functions.find(id);
    if (it == owner->second->_instances->functions.end()) {
        return;
    }
    ++it->second.refs;
}

void LuaScriptingSystem::releaseFunction(uint64_t id)
{
    const auto owner = liveOwners().fnOwner.find(id);
    if (owner == liveOwners().fnOwner.end() || !owner->second || !owner->second->_instances) {
        return;
    }
    const auto it = owner->second->_instances->functions.find(id);
    if (it == owner->second->_instances->functions.end() || it->second.refs <= 0) {
        return;
    }
    if (--it->second.refs > 0) {
        return;
    }
    owner->second->_instances->functions.erase(it);
    liveOwners().fnOwner.erase(id);
}

void LuaScriptingSystem::invokeFunction(uint64_t id, uint64_t selfRuntimeId, script::ScriptArgs args)
{
    const auto owner = liveOwners().fnOwner.find(id);
    if (owner == liveOwners().fnOwner.end() || !owner->second || !owner->second->_instances) {
        YA_CORE_ERROR("script function {} is gone", id);
        return;
    }
    LuaScriptingSystem* system = owner->second;
    const auto          it     = system->_instances->functions.find(id);
    if (it == system->_instances->functions.end() || !it->second.fn.valid()) {
        YA_CORE_ERROR("script function {} is gone", id);
        return;
    }
    const sol::function function = it->second.fn;
    lua_State* const    L        = function.lua_state();
    std::vector<sol::object> arguments;
    arguments.reserve(args.size() + 1);
    if (selfRuntimeId != 0) {
        const sol::table self = system->selfOf(selfRuntimeId);
        if (!self.valid()) {
            YA_CORE_ERROR("script function {} self {} is gone", id, selfRuntimeId);
            return;
        }
        self.push(L);
        arguments.emplace_back(L, -1);
        lua_pop(L, 1);
    }
    for (const script::ScriptValue& value : args) {
        pushLuaValue(L, value);
        arguments.emplace_back(L, -1);
        lua_pop(L, 1);
    }
    const sol::protected_function_result result = protectedCall(function, sol::as_args(arguments));
    if (!result.valid()) {
        const sol::error error = result;
        YA_CORE_ERROR("script function {} error: {}", id, error.what());
    }
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
    registerEntityScriptCall();

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
        "isActionPressed",
        &LuaInputApi::isActionPressed,
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

    // The `Persist` global exists from state build, so scripts can read it
    // before their first write (rpg R3).
    (void)persistentState();

    // 启用脚本热重载
    enableHotReload();
}

void LuaScriptingSystem::onUpdate(float deltaTime)
{
    YA_PROFILE_FUNCTION();

    auto *scene = _services.activeScene ? _services.activeScene() : nullptr;
    if (!scene) return;

    const std::vector<FScriptSlot> slots =
        collectWorldSlots(*scene, treeRanks(*scene), [this](entt::entity handle, ScriptInstance& script) {
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
        if (ScriptInstance* script = findScript(*scene, entity.getHandle(), id)) {
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
            if (script.runtimeId != 0) {
                dropLuaScriptLive(script.runtimeId);
            }
            script.bLoaded = false;
            script.bAuthoringPreviewAttempted = false;
            script.bAuthoringPreviewLoaded = false;
            script.properties.clear();
            script.runtimeId = 0;
        }
    }
}

sol::table LuaScriptingSystem::persistentState()
{
    if (!_persistent.valid()) {
        _persistent = _lua.create_table();
        _lua["Persist"] = _persistent;
    }
    return _persistent;
}

void LuaScriptingSystem::resetPersistentState()
{
    _persistent = _lua.create_table();
    _lua["Persist"] = _persistent;
}

LuaScriptingSystem::LuaScriptingSystem()
    : _instances(std::make_unique<LuaInstanceStore>())
{
    setDropScriptInstance(&dropLuaScriptLive);
    liveOwners().byState.insert_or_assign(_lua.lua_state(), this);
}

LuaScriptingSystem::~LuaScriptingSystem()
{
    // Hosts must destroy their instances while their storage is alive (play
    // stop, entity destroy). Anything left here may point at freed storage, so
    // it is only dropped, never resolved. Sol objects are released here, while
    // `_lua` is still alive (member destructors run after this body).
    liveOwners().byState.erase(_lua.lua_state());
    if (_instances) {
        for (const auto& [id, held] : _instances->functions) {
            (void)held;
            liveOwners().fnOwner.erase(id);
        }
        _instances->functions.clear();
    }
    if (!_live.empty()) {
        YA_CORE_WARN("LuaScriptingSystem: {} script instance(s) still live at shutdown", _live.size());
    }
    _live.clear();
    if (_instances) {
        for (const auto& [id, live] : _instances->byId) {
            (void)live;
            liveOwners().byId.erase(id);
        }
        _instances->byId.clear();
    }
}

LuaInstanceLive* LuaScriptingSystem::findLive(uint64_t runtimeId)
{
    if (!_instances) {
        return nullptr;
    }
    const auto it = _instances->byId.find(runtimeId);
    return it == _instances->byId.end() ? nullptr : &it->second;
}

const LuaInstanceLive* LuaScriptingSystem::findLive(uint64_t runtimeId) const
{
    if (!_instances) {
        return nullptr;
    }
    const auto it = _instances->byId.find(runtimeId);
    return it == _instances->byId.end() ? nullptr : &it->second;
}

void LuaScriptingSystem::eraseLive(uint64_t runtimeId)
{
    if (_instances) {
        _instances->byId.erase(runtimeId);
    }
    liveOwners().byId.erase(runtimeId);
}

bool LuaScriptingSystem::readSource(const std::string& path, std::string& out) const
{
    if (_services.readScript) {
        return _services.readScript(path, out);
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    return vfs && vfs->readFileToString(path, out);
}

bool LuaScriptingSystem::bindChunk(ScriptInstance& instance, const std::string& source)
{
    const bool     bFirst = instance.runtimeId == 0;
    const uint64_t keptId = instance.runtimeId;
    try {
        // Shared global environment so require() and common helper modules
        // work; a script returns a local table to avoid polluting globals.
        sol::table scriptTable = _lua.script(source);
        LuaInstanceLive bound;
        bound.self      = std::move(scriptTable);
        bound.listeners = std::make_shared<ListenerOwner>();
        bound.onInit    = functionField(_lua, bound.self, "onInit");
        bound.onStart   = functionField(_lua, bound.self, "onStart");
        bound.onUpdate  = functionField(_lua, bound.self, "onUpdate");
        bound.onDestroy = functionField(_lua, bound.self, "onDestroy");
        bound.onEnable  = functionField(_lua, bound.self, "onEnable");
        bound.onDisable = functionField(_lua, bound.self, "onDisable");
        const int order = bound.self.get<sol::optional<int>>("executionOrder").value_or(0);

        const uint64_t id = bFirst ? liveOwners().nextId++ : keptId;
        if (bFirst) {
            instance.runtimeId       = id;
            liveOwners().byId[id]    = this;
        }
        _instances->byId.insert_or_assign(id, std::move(bound));
        instance.scriptExecutionOrder = order;
        return true;
    }
    catch (const sol::error& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", instance.scriptPath, e.what());
        if (bFirst && instance.runtimeId != 0) {
            eraseLive(instance.runtimeId);
            instance.runtimeId = 0;
        }
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Lua script error ({}): {}", instance.scriptPath, e.what());
        if (bFirst && instance.runtimeId != 0) {
            eraseLive(instance.runtimeId);
            instance.runtimeId = 0;
        }
    }
    return false;
}

sol::table LuaScriptingSystem::selfOf(uint64_t runtimeId) const
{
    const LuaInstanceLive* live = findLive(runtimeId);
    if (!live || !live->self.valid() || live->self.lua_state() != _lua.lua_state()) {
        return {};
    }
    return live->self;
}

void LuaScriptingSystem::dropInstance(uint64_t runtimeId)
{
    if (runtimeId == 0) {
        return;
    }
    auto node = _live.extract(runtimeId);
    eraseLive(runtimeId);
    if (node.empty()) {
        return;
    }
    if (ScriptInstance* current = node.mapped()->resolve(runtimeId)) {
        current->bLoaded   = false;
        current->runtimeId = 0;
    }
}

void dropLuaScriptLive(uint64_t runtimeId)
{
    if (runtimeId == 0) {
        return;
    }
    const auto it = liveOwners().byId.find(runtimeId);
    if (it == liveOwners().byId.end() || !it->second) {
        return;
    }
    it->second->dropInstance(runtimeId);
}

IScriptInstanceHost* LuaScriptingSystem::hostOf(const ScriptInstance& instance) const
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

bool LuaScriptingSystem::load(ScriptInstance& instance, std::unique_ptr<IScriptInstanceHost> host)
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
        return false;
    }
    LuaInstanceLive* live = findLive(instance.runtimeId);
    if (!live) {
        dropInstance(instance.runtimeId);
        return false;
    }
    applyHostSelf(live->self, *host);
    captureScriptProperties(instance, live->self);
    applyScriptPropertyOverrides(instance, live->self, _lua);
    instance.bLoaded = true;
    _live.emplace(instance.runtimeId, std::move(host));
    YA_CORE_INFO("Loaded Lua script: {}", instance.scriptPath);
    return true;
}

bool LuaScriptingSystem::call(ScriptInstance& instance, ELuaScriptCallback which, float deltaTime)
{
    IScriptInstanceHost*  host = hostOf(instance);
    LuaInstanceLive* live = findLive(instance.runtimeId);
    if (!host || !instance.bLoaded || !live) {
        return false;
    }
    applyHostSelf(live->self, *host);
    // Copied out of the side table: the callback may destroy this instance and
    // drop the entry. Nothing below reads `instance` (its storage may move).
    const std::string path = instance.scriptPath;
    sol::function     callback;
    const char*       what = "";
    switch (which) {
    case ELuaScriptCallback::Init:
    {
        callback = live->onInit;
        what     = "onInit";
    } break;
    case ELuaScriptCallback::Start:
    {
        callback = live->onStart;
        what     = "onStart";
    } break;
    case ELuaScriptCallback::Update:
    {
        callback = live->onUpdate;
        what     = "onUpdate";
    } break;
    case ELuaScriptCallback::Destroy:
    {
        callback = live->onDestroy;
        what     = "onDestroy";
    } break;
    }
    const sol::table self = live->self;
    if (which == ELuaScriptCallback::Update) {
        invokeLuaCallback(callback, what, path, self, deltaTime);
    }
    else {
        invokeLuaCallback(callback, what, path, self);
    }
    return true;
}

script::ScriptValue LuaScriptingSystem::callNamed(ScriptInstance& instance, const std::string& name,
                                                  script::ScriptArgs args, ENamedCallError onError)
{
    IScriptInstanceHost*  host = hostOf(instance);
    LuaInstanceLive* live = findLive(instance.runtimeId);
    if (!host || !instance.bLoaded || !live) {
        return {};
    }
    applyHostSelf(live->self, *host);
    sol::table self = live->self;
    if (!functionField(_lua, self, name.c_str()).valid()) {
        return {};
    }
    return callDefinedFunction(instance, std::move(self), name, args, onError);
}

void LuaScriptingSystem::destroy(ScriptInstance& instance)
{
    const uint64_t id   = instance.runtimeId;
    auto           node = id != 0 ? _live.extract(id) : decltype(_live)::node_type{};
    if (node.empty()) {
        return;
    }
    // Unregistered before onDestroy runs, so a callback that destroys the same
    // instance again is a no-op instead of a recursion.
    std::unique_ptr<IScriptInstanceHost> host = std::move(node.mapped());
    if (instance.bLoaded) {
        if (LuaInstanceLive* live = findLive(id)) {
            applyHostSelf(live->self, *host);
            const std::string   path      = instance.scriptPath;
            const sol::function onDestroy = live->onDestroy;
            const sol::table    self      = live->self;
            invokeLuaCallback(onDestroy, "onDestroy", path, self);
        }
    }
    eraseLive(id);
    if (ScriptInstance* current = host->resolve(id)) {
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
        if (ScriptInstance* instance = it->second->resolve(id)) {
            destroy(*instance);
        }
        else {
            eraseLive(id);
            _live.erase(it);
        }
    }
}

bool LuaScriptingSystem::reloadInstance(IScriptInstanceHost* host, uint64_t id, const std::string& source)
{
    ScriptInstance* instance = host->resolve(id);
    if (!instance) {
        return false;
    }

    LuaInstanceLive* live = findLive(id);
    if (!live) {
        return false;
    }
    std::unordered_map<std::string, sol::object> savedProperties;
    for (const auto& prop : instance->properties) {
        savedProperties[prop.name] = live->self[prop.name];
    }
    call(*instance, ELuaScriptCallback::Destroy);

    // Callbacks may have destroyed the instance or moved its storage.
    instance = host->resolve(id);
    if (!instance || !bindChunk(*instance, source)) {
        return false;
    }
    live = findLive(id);
    if (!live) {
        return false;
    }
    applyHostSelf(live->self, *host);
    captureScriptProperties(*instance, live->self);
    for (const auto& [name, value] : savedProperties) {
        if (value.valid()) {
            live->self[name] = value;
        }
    }
    applyScriptPropertyOverrides(*instance, live->self, _lua);

    // A reloaded instance is a batch of one: onInit, then onStart.
    call(*instance, ELuaScriptCallback::Init);
    if (ScriptInstance* started = host->resolve(id)) {
        call(*started, ELuaScriptCallback::Start);
    }
    return true;
}

void LuaScriptingSystem::reloadScript(const std::string& scriptPath)
{
    const std::string path = ScriptInstance::normalizeScriptPath(scriptPath);
    YA_CORE_INFO("[Hot Reload] Reloading script: {}", path);

    std::string source;
    bool        bSourceRead = false;
    for (const uint64_t id : liveIds()) {
        auto it = _live.find(id);
        if (it == _live.end()) {
            continue;
        }
        IScriptInstanceHost*    host     = it->second.get();
        ScriptInstance* instance = host->resolve(id);
        if (!instance) {
            eraseLive(id);
            _live.erase(it);
            continue;
        }
        if (ScriptInstance::normalizeScriptPath(instance->scriptPath) != path) {
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
        if (it->second->resolve(it->first)) {
            it = std::next(it);
            continue;
        }
        eraseLive(it->first);
        it = _live.erase(it);
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

namespace
{

LuaScriptingSystem* asLuaRuntime(IWorldScriptRuntime& runtime)
{
    auto* concrete = dynamic_cast<LuaScriptingSystem*>(&runtime);
    if (!concrete) {
        YA_CORE_ERROR("script instance op: world script runtime is not the Lua backend");
    }
    return concrete;
}

} // namespace

bool loadScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance, std::unique_ptr<IScriptInstanceHost> host)
{
    LuaScriptingSystem* system = asLuaRuntime(runtime);
    return system ? system->load(instance, std::move(host)) : false;
}

bool callScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance, EScriptCallback which, float deltaTime)
{
    LuaScriptingSystem* system = asLuaRuntime(runtime);
    if (!system) {
        return false;
    }
    ELuaScriptCallback callback = ELuaScriptCallback::Init;
    switch (which) {
    case EScriptCallback::Init:    callback = ELuaScriptCallback::Init; break;
    case EScriptCallback::Start:   callback = ELuaScriptCallback::Start; break;
    case EScriptCallback::Update:  callback = ELuaScriptCallback::Update; break;
    case EScriptCallback::Destroy: callback = ELuaScriptCallback::Destroy; break;
    }
    return system->call(instance, callback, deltaTime);
}

script::ScriptValue callScriptInstanceNamed(IWorldScriptRuntime& runtime, ScriptInstance& instance, const std::string& name,
                                            script::ScriptArgs args, bool bSwallowErrors)
{
    LuaScriptingSystem* system = asLuaRuntime(runtime);
    if (!system) {
        return {};
    }
    return system->callNamed(instance, name, args, bSwallowErrors ? ENamedCallError::Swallow : ENamedCallError::Throw);
}

void destroyScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance)
{
    if (LuaScriptingSystem* system = asLuaRuntime(runtime)) {
        system->destroy(instance);
    }
}

std::shared_ptr<void> scriptInstanceListenerOwner(uint64_t runtimeId)
{
    const auto owner = liveOwners().byId.find(runtimeId);
    if (owner == liveOwners().byId.end() || !owner->second) {
        return {};
    }
    const LuaInstanceLive* live = owner->second->findLive(runtimeId);
    if (!live || !live->listeners) {
        return {};
    }
    return live->listeners;
}

bool retainInstanceFunction(uint64_t runtimeId, script::ScriptFnHandle fn)
{
    if (fn.id == 0 || !scriptFunctionAlive(fn)) {
        return false;
    }
    const auto owner = liveOwners().byId.find(runtimeId);
    if (owner == liveOwners().byId.end() || !owner->second) {
        return false;
    }
    LuaInstanceLive* live = owner->second->findLive(runtimeId);
    if (!live || !live->listeners) {
        return false;
    }
    LuaScriptingSystem::retainFunction(fn.id);
    live->listeners->functions.push_back(fn);
    return true;
}

bool scriptFunctionAlive(script::ScriptFnHandle fn)
{
    if (fn.id == 0) {
        return false;
    }
    const auto held = liveOwners().fnOwner.find(fn.id);
    if (held == liveOwners().fnOwner.end() || !held->second || !held->second->_instances) {
        return false;
    }
    return held->second->_instances->functions.contains(fn.id);
}

} // namespace ya
