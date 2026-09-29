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
/// Metatable key that tells script objects from other userdata.
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
// The only code in this file that addresses the Lua stack by index. The
// metamethods below read their call through ObjectCall / MethodCall.
// ============================================================================

const ScriptRef& refAt(lua_State* L, int index)
{
    return *static_cast<const ScriptRef*>(lua_touserdata(L, index));
}

/// Pushes the metatable registered for `type`; pushes nothing when there is none.
bool pushRegisteredMetatable(lua_State* L, type_index_t type)
{
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &kMetatablesKey) != LUA_TTABLE) {
        lua_pop(L, 1);
        return false;
    }
    const bool bFound = lua_rawgeti(L, -1, static_cast<lua_Integer>(type)) == LUA_TTABLE;
    lua_remove(L, -2);
    if (!bFound) {
        lua_pop(L, 1);
    }
    return bFound;
}

void pushObjectMetatable(lua_State* L, type_index_t type);

void pushObject(lua_State* L, const ScriptRef& ref)
{
    new (lua_newuserdatauv(L, sizeof(ScriptRef), 0)) ScriptRef(ref);
    pushObjectMetatable(L, ref.type);
    lua_setmetatable(L, -2);
}

bool hasObjectMarker(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index)) {
        return false;
    }
    const bool bObject = lua_rawgetp(L, -1, &kObjectMarker) == LUA_TBOOLEAN;
    lua_pop(L, 2);
    return bObject;
}

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
    [[nodiscard]] bool             calledOnOwner() const { return hasObjectMarker(_L, kSelf) && self().type == ownerType(); }
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
    sol::stack::push(L,
                     sol::make_closure(&guarded<&methodCall>,
                                       sol::lightuserdata_value(const_cast<ScriptMethod*>(&method)),
                                       sol::stack_object(L, nameIndex),
                                       static_cast<lua_Integer>(type)));
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

sol::table registeredMetatables(sol::state_view lua)
{
    sol::table                 registry = lua.registry();
    const auto                 key      = sol::lightuserdata_value(&kMetatablesKey);
    sol::optional<sol::table> types    = registry.raw_get<sol::optional<sol::table>>(key);
    if (types) {
        return *types;
    }
    sol::table created = lua.create_table();
    registry.raw_set(key, created);
    return created;
}

void pushObjectMetatable(lua_State* L, type_index_t type)
{
    if (pushRegisteredMetatable(L, type)) {
        return;
    }

    sol::state_view lua(L);
    sol::table      memberCache = lua.create_table();
    sol::table      metatable   = lua.create_table_with(
        sol::meta_function::index,
        sol::make_closure(&guarded<&objectIndex>, memberCache),
        sol::meta_function::new_index,
        sol::make_closure(&guarded<&objectNewIndex>, memberCache),
        sol::meta_function::equal_to,
        [](const sol::stack_object& a, const sol::stack_object& b) {
            return a.is<LuaScriptObject>() && b.is<LuaScriptObject>() && a.as<LuaScriptObject>().ref == b.as<LuaScriptObject>().ref;
        },
        sol::meta_function::to_string,
        [](LuaScriptObject self) { return typeNameOf(self.ref.type); },
        // Hides the metamethods from scripts: they trust argument 1 to be ours.
        sol::meta_function::metatable,
        false,
        "__name",
        typeNameOf(type));
    metatable.raw_set(sol::lightuserdata_value(&kObjectMarker), true);
    registeredMetatables(lua).raw_set(static_cast<lua_Integer>(type), metatable);
    sol::stack::push(L, metatable);
}

} // namespace

bool isLuaScriptObject(lua_State* L, int index)
{
    return hasObjectMarker(L, index);
}

int sol_lua_push(lua_State* L, const LuaScriptObject& object)
{
    if (!object.ref) {
        return sol::stack::push(L, sol::lua_nil);
    }
    pushObject(L, object.ref);
    return 1;
}

LuaScriptObject sol_lua_get(sol::types<LuaScriptObject>, lua_State* L, int index, sol::stack::record& tracking)
{
    tracking.use(1);
    return LuaScriptObject{refAt(L, index)};
}

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
}

int pushLuaValue(lua_State* L, const ScriptValue& value)
{
    return std::visit(
        [L](const auto& v) -> int {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return sol::stack::push(L, sol::lua_nil);
            }
            else if constexpr (std::is_same_v<T, ScriptRef>) {
                return sol::stack::push(L, LuaScriptObject{v});
            }
            else {
                return sol::stack::push(L, v);
            }
        },
        value);
}

ScriptValue toScriptValue(lua_State* L, int index)
{
    const sol::stack_object value(L, index);
    switch (value.get_type()) {
    case sol::type::lua_nil:
    case sol::type::none:
        return {};
    case sol::type::boolean:
        return value.as<bool>();
    case sol::type::number:
        if (lua_isinteger(L, index)) {
            return value.as<int64_t>();
        }
        return value.as<double>();
    case sol::type::string:
        return value.as<std::string>();
    case sol::type::userdata:
        if (value.is<LuaScriptObject>()) {
            return value.as<LuaScriptObject>().ref;
        }
        if (value.is<glm::vec3>()) {
            return value.as<glm::vec3>();
        }
        if (value.is<glm::vec2>()) {
            return value.as<glm::vec2>();
        }
        if (value.is<glm::vec4>()) {
            return value.as<glm::vec4>();
        }
        break;
    default:
        break;
    }
    throw ScriptError(std::format("a Lua {} cannot be passed to the engine", sol::type_name(L, value.get_type())));
}

} // namespace ya
