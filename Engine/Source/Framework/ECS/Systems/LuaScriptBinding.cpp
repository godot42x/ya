// Engine objects in Lua. The whole mechanism, one screen:
//
// C++ -> Lua: anything that moves a LuaScriptObject through sol ends up in
// sol_lua_push -> pushObject: one full userdata holding a ScriptRef, tagged
// with the metatable of ref.type (registry[&kMetatablesKey][type], built on
// first push, see pushObjectMetatable).
//
// Lua -> C++: Lua calls the closures the metatable carries:
//   obj.x        -> __index    = objectIndex
//   obj.x = v    -> __newindex = objectNewIndex
//   obj:foo(...) -> the method closure objectIndex cached under "foo"
//                   (= methodCall)
// Both metamethods share the type's member cache (their upvalue): on the
// first access of a name they ask the neutral layer (script::findField /
// findMethod) and rawset the answer into the cache — fields as lightuserdata
// handles, methods as closures whose upvalues are the handle, the name and
// the owning type. Later accesses are one table lookup. Field values still
// resolve per access, so an object that is gone raises instead of dangling.
//
// Module functions (script::registerModuleFunction) become globals
// `<module>.<name>` when the state is built: closures over the registered
// entry (moduleFunctionCall).
//
// sol remains in exactly two places: the bridge functions below (other
// modules move engine objects through sol) and the Vec2/3/4 value types,
// which stay sol usertypes end to end (registration, conversion, checks).

#include "ECS/Systems/LuaScriptBinding.h"

#include "Core/Scripting/ScriptBindings.h"
#include "Scene/Core/SceneScriptBindings.h"

#include <glm/glm.hpp>

#include <format>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ya
{

namespace
{

using script::ScriptError;
using script::ScriptField;
using script::ScriptMethod;
using script::ScriptRef;
using script::ScriptValue;

static_assert(std::is_trivially_copyable_v<ScriptRef> && std::is_trivially_destructible_v<ScriptRef>,
              "script object userdata has no __gc");

/// Registry key of the table type index -> metatable, one per Lua state.
char kMetatablesKey = 0;
/// Metatable key that tells script object userdata from other userdata.
char kObjectMarker = 0;

std::string typeNameOf(type_index_t type)
{
    std::string name = script::scriptTypeName(type);
    return name.empty() ? std::string("object") : name;
}

/// Runs `Impl`, turning a C++ exception into a Lua error only after every C++
/// object of the call is destroyed (lua_error does not unwind C++ frames).
template <int (*Impl)(lua_State*)>
int guarded(lua_State* L)
{
    int results = -1;
    try {
        results = Impl(L);
    }
    catch (const std::exception& e) {
        lua_pushstring(L, e.what());
    }
    return results >= 0 ? results : lua_error(L);
}

// ============================================================================
// Stack primitives: userdata access, registry tables, object construction.
// ============================================================================

const ScriptRef& refAt(lua_State* L, int index)
{
    return *static_cast<const ScriptRef*>(lua_touserdata(L, index));
}

/// True when the userdata's metatable carries `marker`.
bool metatableHasMarker(lua_State* L, int index, const void* marker)
{
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index)) {
        return false;
    }
    const bool bHas = lua_rawgetp(L, -1, marker) != LUA_TNIL;
    lua_pop(L, 2);
    return bHas;
}

/// Pushes the per-state table type index -> metatable, creating it on first use.
void pushMetatableTable(lua_State* L)
{
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &kMetatablesKey) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kMetatablesKey);
    }
}

/// Pushes the metatable registered for `type`, creating it on first use.
void pushObjectMetatable(lua_State* L, type_index_t type);

void pushObject(lua_State* L, const ScriptRef& ref)
{
    new (lua_newuserdatauv(L, sizeof(ScriptRef), 0)) ScriptRef(ref);
    pushObjectMetatable(L, ref.type);
    lua_setmetatable(L, -2);
}

/// The file's field-setting vocabulary. Every helper resolves `table` to an
/// absolute index first, so the pushes inside never shift the target.
void setFieldFunction(lua_State* L, int table, const char* name, lua_CFunction fn)
{
    const int target = lua_absindex(L, table);
    lua_pushcfunction(L, fn);
    lua_setfield(L, target, name);
}

/// table[name] = closure(impl) whose single upvalue is a copy of `upvalue`.
void setFieldClosure(lua_State* L, int table, const char* name, int (*impl)(lua_State*), int upvalue)
{
    const int target = lua_absindex(L, table);
    const int source = lua_absindex(L, upvalue);
    lua_pushvalue(L, source);
    lua_pushcclosure(L, impl, 1);
    lua_setfield(L, target, name);
}

void setFieldString(lua_State* L, int table, const char* name, std::string_view text)
{
    const int target = lua_absindex(L, table);
    lua_pushlstring(L, text.data(), text.size());
    lua_setfield(L, target, name);
}

void setFieldBoolean(lua_State* L, int table, const char* name, bool value)
{
    const int target = lua_absindex(L, table);
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, target, name);
}

/// table[lightuserdata marker] = true, findable only by pointer.
void setMarker(lua_State* L, int table, const void* marker)
{
    const int target = lua_absindex(L, table);
    lua_pushboolean(L, 1);
    lua_rawsetp(L, target, marker);
}

// ============================================================================
// The two calls a script can make on an engine object, read from the stack.
// ============================================================================

/// What a method closure sees: the object and the script's arguments, plus
/// the method handle, its name and the type it belongs to as upvalues.
class MethodCall
{
    static constexpr int kSelf          = 1;
    static constexpr int kFirstArgument = 2;

    lua_State* _L;

  public:
    explicit MethodCall(lua_State* L) : _L(L) {}

    /// Pushes a closure that calls `method` of `type`, named `nameIndex`'s string.
    static void push(lua_State* L, const ScriptMethod& method, int nameIndex, type_index_t type);

    [[nodiscard]] const ScriptMethod& method() const
    {
        return *static_cast<const ScriptMethod*>(lua_touserdata(_L, lua_upvalueindex(1)));
    }
    [[nodiscard]] const char*  name() const { return lua_tostring(_L, lua_upvalueindex(2)); }
    [[nodiscard]] type_index_t ownerType() const { return static_cast<type_index_t>(lua_tointeger(_L, lua_upvalueindex(3))); }

    /// False for `obj.method()` and for a self of another type.
    [[nodiscard]] bool             calledOnOwner() const { return metatableHasMarker(_L, kSelf, &kObjectMarker) && self().type == ownerType(); }
    [[nodiscard]] const ScriptRef& self() const { return refAt(_L, kSelf); }

    [[nodiscard]] std::vector<ScriptValue> arguments() const
    {
        std::vector<ScriptValue> args;
        const int                last = lua_gettop(_L);
        args.reserve(last >= kFirstArgument ? last - kFirstArgument + 1 : 0);
        for (int index = kFirstArgument; index <= last; ++index) {
            args.push_back(toScriptValue(_L, index));
        }
        return args;
    }

    int returnValue(const ScriptValue& value) const { return pushLuaValue(_L, value); }
};

/// What a module function closure (`world.find`) sees: the script's
/// arguments, plus the registered function as upvalue.
class ModuleFunctionCall
{
    static constexpr int kFirstArgument = 1;

    lua_State* _L;

  public:
    explicit ModuleFunctionCall(lua_State* L) : _L(L) {}

    [[nodiscard]] const script::ScriptModuleFunction& function() const
    {
        return *static_cast<const script::ScriptModuleFunction*>(lua_touserdata(_L, lua_upvalueindex(1)));
    }

    [[nodiscard]] std::vector<ScriptValue> arguments() const
    {
        std::vector<ScriptValue> args;
        const int                last = lua_gettop(_L);
        args.reserve(last >= kFirstArgument ? last - kFirstArgument + 1 : 0);
        for (int index = kFirstArgument; index <= last; ++index) {
            args.push_back(toScriptValue(_L, index));
        }
        return args;
    }

    int returnValue(const ScriptValue& value) const { return pushLuaValue(_L, value); }
};

/// A hit in a type's member cache.
struct CachedMember
{
    /// The method's closure is left pushed, as the result of the call.
    bool               bMethod = false;
    const ScriptField* field   = nullptr;
};

/// What `__index` / `__newindex` on a script object sees: the object, the
/// member key and (for `__newindex`) the assigned value, plus the member cache
/// of the object's type as upvalue: name -> method closure or field handle.
class ObjectCall
{
    static constexpr int kSelf  = 1;
    static constexpr int kKey   = 2;
    static constexpr int kValue = 3;

    lua_State* _L;

    static int cache() { return lua_upvalueindex(1); }

  public:
    explicit ObjectCall(lua_State* L) : _L(L) {}

    [[nodiscard]] const ScriptRef& self() const { return refAt(_L, kSelf); }
    [[nodiscard]] ScriptValue      assignedValue() const { return toScriptValue(_L, kValue); }

    /// Empty for keys that are not strings; no member has such a name.
    [[nodiscard]] std::optional<std::string_view> keyName() const
    {
        if (lua_type(_L, kKey) != LUA_TSTRING) {
            return std::nullopt;
        }
        size_t      length = 0;
        const char* text   = lua_tolstring(_L, kKey, &length);
        return std::string_view(text, length);
    }

    [[nodiscard]] std::string keyText() const
    {
        const auto name = keyName();
        return name ? std::string(*name) : std::format("<{}>", luaL_typename(_L, kKey));
    }

    [[nodiscard]] CachedMember lookupCache() const
    {
        lua_pushvalue(_L, kKey);
        switch (lua_rawget(_L, cache())) {
        case LUA_TFUNCTION:
            return {.bMethod = true};
        case LUA_TLIGHTUSERDATA: {
            const auto* field = static_cast<const ScriptField*>(lua_touserdata(_L, -1));
            lua_pop(_L, 1);
            return {.field = field};
        }
        default:
            lua_pop(_L, 1);
            return {};
        }
    }

    void cacheField(const ScriptField& field) const
    {
        lua_pushvalue(_L, kKey);
        lua_pushlightuserdata(_L, const_cast<ScriptField*>(&field));
        lua_rawset(_L, cache());
    }

    /// Leaves the method's closure pushed, as the result of the call.
    void pushAndCacheMethod(const ScriptMethod& method) const
    {
        MethodCall::push(_L, method, kKey, self().type);
        lua_pushvalue(_L, kKey);
        lua_pushvalue(_L, -2);
        lua_rawset(_L, cache());
    }

    static int returnPushedMethod() { return 1; }
    int        returnValue(const ScriptValue& value) const { return pushLuaValue(_L, value); }
};

// ============================================================================
// Metamethods
// ============================================================================

int methodCall(lua_State* L)
{
    const MethodCall call(L);
    if (!call.calledOnOwner()) {
        throw ScriptError(std::format("call '{}' with ':' on a {}", call.name(), typeNameOf(call.ownerType())));
    }
    return call.returnValue(script::callMethod(call.self(), call.method(), call.arguments()));
}

void MethodCall::push(lua_State* L, const ScriptMethod& method, int nameIndex, type_index_t type)
{
    lua_pushlightuserdata(L, const_cast<ScriptMethod*>(&method)); // upvalue 1: the handle
    lua_pushvalue(L, nameIndex);                                  // upvalue 2: the name
    lua_pushinteger(L, static_cast<lua_Integer>(type));           // upvalue 3: the owning type
    lua_pushcclosure(L, &guarded<&methodCall>, 3);
}

int objectIndex(lua_State* L)
{
    const ObjectCall   call(L);
    const CachedMember cached = call.lookupCache();
    if (cached.bMethod) {
        return ObjectCall::returnPushedMethod();
    }
    if (cached.field) {
        return call.returnValue(script::readField(call.self(), *cached.field));
    }

    if (const auto name = call.keyName()) {
        if (const ScriptField* field = script::findField(call.self().type, *name)) {
            call.cacheField(*field);
            return call.returnValue(script::readField(call.self(), *field));
        }
        if (const ScriptMethod* method = script::findMethod(call.self().type, *name)) {
            call.pushAndCacheMethod(*method);
            return ObjectCall::returnPushedMethod();
        }
    }
    throw ScriptError(std::format("{} has no field or method '{}'", typeNameOf(call.self().type), call.keyText()));
}

int objectNewIndex(lua_State* L)
{
    const ObjectCall   call(L);
    const CachedMember cached = call.lookupCache();
    const ScriptField* field  = cached.field;
    if (!field && !cached.bMethod) {
        if (const auto name = call.keyName()) {
            field = script::findField(call.self().type, *name);
            if (field) {
                call.cacheField(*field);
            }
        }
    }
    if (!field) {
        throw ScriptError(std::format("{} has no field '{}'", typeNameOf(call.self().type), call.keyText()));
    }
    script::writeField(call.self(), *field, call.assignedValue());
    return 0;
}

int moduleFunctionCall(lua_State* L)
{
    const ModuleFunctionCall            call(L);
    const script::ScriptModuleFunction& function = call.function();
    try {
        return call.returnValue(function.fn(call.arguments()));
    }
    catch (const std::exception& e) {
        throw ScriptError(std::format("{}.{}: {}", function.module, function.name, e.what()));
    }
}

/// Global table `name`, created on first use. Leaves it pushed.
void pushGlobalTable(lua_State* L, const std::string& name)
{
    if (lua_getglobal(L, name.c_str()) == LUA_TTABLE) {
        return;
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, name.c_str());
}

/// `<module>.<name>` = a closure over the registered function.
void setModuleFunction(lua_State* L, const script::ScriptModuleFunction& function)
{
    pushGlobalTable(L, function.module);
    const int moduleTable = lua_absindex(L, -1);
    lua_pushlightuserdata(L, const_cast<script::ScriptModuleFunction*>(&function));
    lua_pushcclosure(L, &guarded<&moduleFunctionCall>, 1);
    lua_setfield(L, moduleTable, function.name.c_str());
    lua_pop(L, 1); // the module table
}

int objectEq(lua_State* L)
{
    lua_pushboolean(L, metatableHasMarker(L, 1, &kObjectMarker) && metatableHasMarker(L, 2, &kObjectMarker) && refAt(L, 1) == refAt(L, 2));
    return 1;
}

int objectToString(lua_State* L)
{
    const std::string name = typeNameOf(refAt(L, 1).type);
    lua_pushlstring(L, name.data(), name.size());
    return 1;
}

/// The metatable every script object of `type` carries: the two member
/// closures sharing the type's member cache, identity/string comparison, all
/// hidden from scripts and marked as ours. Leaves the metatable pushed.
void pushObjectMetatable(lua_State* L, type_index_t type)
{
    pushMetatableTable(L);
    const int typesTable = lua_absindex(L, -1);
    if (lua_rawgeti(L, typesTable, static_cast<lua_Integer>(type)) == LUA_TTABLE) {
        lua_remove(L, typesTable);
        return;
    }
    lua_pop(L, 1);

    lua_newtable(L); // the metatable
    const int metatable = lua_absindex(L, -1);

    lua_createtable(L, 0, 0); // the member cache the two closures share
    const int memberCache = lua_absindex(L, -1);
    setFieldClosure(L, metatable, "__index", &guarded<&objectIndex>, memberCache);
    setFieldClosure(L, metatable, "__newindex", &guarded<&objectNewIndex>, memberCache);
    lua_pop(L, 1); // the cache lives on only inside the closures

    setFieldFunction(L, metatable, "__eq", &objectEq);
    setFieldFunction(L, metatable, "__tostring", &guarded<&objectToString>);
    setFieldString(L, metatable, "__name", typeNameOf(type));
    // Hides the metamethods from scripts: they trust argument 1 to be ours.
    setFieldBoolean(L, metatable, "__metatable", false);
    setMarker(L, metatable, &kObjectMarker);

    lua_pushvalue(L, metatable);
    lua_rawseti(L, typesTable, static_cast<lua_Integer>(type)); // types[type] = metatable
    lua_remove(L, typesTable);                                  // leave only the metatable pushed
}

} // namespace

// ============================================================================
// sol2 bridge: the only place sol meets engine objects. Every module that
// moves a LuaScriptObject through sol (bindSelf, world.* functions, tests)
// funnels through these three.
// ============================================================================

bool isLuaScriptObject(lua_State* L, int index)
{
    return metatableHasMarker(L, index, &kObjectMarker);
}

int sol_lua_push(lua_State* L, const LuaScriptObject& object)
{
    if (!object.ref) {
        lua_pushnil(L);
        return 1;
    }
    pushObject(L, object.ref);
    return 1;
}

LuaScriptObject sol_lua_get(sol::types<LuaScriptObject>, lua_State* L, int index, sol::stack::record& tracking)
{
    tracking.use(1);
    return LuaScriptObject{refAt(L, index)};
}

// ============================================================================
// Vec value types stay sol usertypes end to end (registration and conversion).
// ============================================================================

void registerLuaScriptBindings(sol::state_view lua)
{
    script::ensureSceneScriptBindings();

    lua.new_usertype<glm::vec2>("Vec2",
                                                  sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
                                                  "x",
                                                  &glm::vec2::x,
                                                  "y",
                                                  &glm::vec2::y,
                                                  "__add",
                                                  [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
                                                  "__sub",
                                                  [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
                                                  "__mul",
                                                  sol::overload([](const glm::vec2& v, float s) { return v * s; },
                                                                [](float s, const glm::vec2& v) { return s * v; }),
                                                  "__div",
                                                  [](const glm::vec2& v, float s) { return v / s; },
                                                  "length",
                                                  [](const glm::vec2& v) { return glm::length(v); },
                                                  "normalize",
                                                  [](const glm::vec2& v) { return glm::normalize(v); });

    lua.new_usertype<glm::vec3>("Vec3",
                                                  sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                                  "x",
                                                  &glm::vec3::x,
                                                  "y",
                                                  &glm::vec3::y,
                                                  "z",
                                                  &glm::vec3::z,
                                                  "__add",
                                                  [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
                                                  "__sub",
                                                  [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
                                                  "__mul",
                                                  sol::overload([](const glm::vec3& v, float s) { return v * s; },
                                                                [](float s, const glm::vec3& v) { return s * v; }),
                                                  "__div",
                                                  [](const glm::vec3& v, float s) { return v / s; },
                                                  "length",
                                                  [](const glm::vec3& v) { return glm::length(v); },
                                                  "normalize",
                                                  [](const glm::vec3& v) { return glm::normalize(v); },
                                                  "dot",
                                                  [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
                                                  "cross",
                                                  [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); });

    lua.new_usertype<glm::vec4>("Vec4",
                                                  sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
                                                  "x",
                                                  &glm::vec4::x,
                                                  "y",
                                                  &glm::vec4::y,
                                                  "z",
                                                  &glm::vec4::z,
                                                  "w",
                                                  &glm::vec4::w);

    script::forEachModuleFunction([L = lua.lua_state()](const script::ScriptModuleFunction& function) {
        setModuleFunction(L, function);
    });
}

// ============================================================================
// ScriptValue <-> the Lua stack
// ============================================================================

int pushLuaValue(lua_State* L, const ScriptValue& value)
{
    return std::visit(
        [L](const auto& v) -> int {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                lua_pushnil(L);
            }
            else if constexpr (std::is_same_v<T, bool>) {
                lua_pushboolean(L, v ? 1 : 0);
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                lua_pushinteger(L, static_cast<lua_Integer>(v));
            }
            else if constexpr (std::is_same_v<T, double>) {
                lua_pushnumber(L, static_cast<lua_Number>(v));
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                lua_pushlstring(L, v.data(), v.size());
            }
            else if constexpr (std::is_same_v<T, ScriptRef>) {
                pushObject(L, v);
            }
            else {
                return sol::stack::push(L, v); // Vec value types are sol usertypes
            }
            return 1;
        },
        value);
}

ScriptValue toScriptValue(lua_State* L, int index)
{
    switch (lua_type(L, index)) {
    case LUA_TNIL:
    case LUA_TNONE:
        return {};
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            return static_cast<int64_t>(lua_tointeger(L, index));
        }
        return static_cast<double>(lua_tonumber(L, index));
    case LUA_TSTRING: {
        size_t      length = 0;
        const char* text   = lua_tolstring(L, index, &length);
        return std::string(text, length);
    }
    case LUA_TUSERDATA:
        if (metatableHasMarker(L, index, &kObjectMarker)) {
            return refAt(L, index);
        }
        // Vec value types are sol usertypes; sol recognizes its own instances.
        if (sol::stack::check<glm::vec3>(L, index, &sol::no_panic)) {
            return sol::stack::get<glm::vec3>(L, index);
        }
        if (sol::stack::check<glm::vec2>(L, index, &sol::no_panic)) {
            return sol::stack::get<glm::vec2>(L, index);
        }
        if (sol::stack::check<glm::vec4>(L, index, &sol::no_panic)) {
            return sol::stack::get<glm::vec4>(L, index);
        }
        break;
    default:
        break;
    }
    throw ScriptError(std::format("a Lua {} cannot be passed to the engine", luaL_typename(L, index)));
}

} // namespace ya