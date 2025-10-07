//
/*
 * @ Author: godot42
 * @ Create Time: 2025-01-03 00:29:21
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-04 02:03:44
 * @ Description:
 */


#pragma once
extern "C" {
#include "lua.h"
}
#include "core.h"

#include "luavar.h"

#include "yalua/manager.h"


#include <typeinfo>

#include "log.h"


#include "utility.cc/string_utils.h"

struct YALUA_API LuaMachine
{
    friend struct LuaMachineManager;

    lua_State *L;
    int        index;

  public:
    bool bDebugOuput = false;

  public:

    LuaMachine(const LuaMachine &) = default;
    LuaMachine(LuaMachine &&other) noexcept
    {
        L           = other.L;
        index       = other.index;
        other.L     = nullptr;
        other.index = -1;
    }
    LuaMachine(lua_State *L, int index);
    virtual ~LuaMachine() = default;

    lua_State *getState() { return L; }
    int        getIndex() { return index; }
    bool       isValid() { return L != nullptr && index > 0; }

    void dump_stack(const std::string &sig = "")
    {
        if (!sig.empty()) {
            printf("%s\n", sig.data());
        }
        if (bDebugOuput) {
            luaL_stack_dump(L);
        }
    }


  public:
    bool LoadFromString(const std::string &str);
    bool LoadLuaScriptFile(const char *filename, int nret = 0);


    void registerGlobalFunc(const char *name, lua_CFunction func)
    {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
        {
            if (lua_error(L)) {
                fprintf(stderr, "lua_register failed: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            if (lua_getglobal(L, name) != LUA_TFUNCTION) {
                YALUA_LOG("lua_register failed: %s\n", lua_tostring(L, -1));
            }
        }
        lua_pop(L, 1);
    }


    bool CallMemberFunc(const char *path, const char *member_func)
    {
        YALUA_LOG("-->>CallLuaMemberFunc %s.%s", path, member_func);
        LuaVar var = LuaVar::GetValue(L, path);
        if (var.type == ELuaType::Nil) {
            YALUA_LOG("failed to get %s\n", path);
            return false;
        }

        if (var.type != ELuaType::Table) {
            YALUA_LOG("failed to get {}, not a table: {}\n", path, (int)var.type);
        }
        bool ret = false;

        lua_pushstring(L, member_func);
        lua_gettable(L, -2);
        if (lua_isfunction(L, -1)) {
            ret = call_luafunc_impl(L, 0, 0);
        }
        YALUA_LOG("--<< end CallLuaMemberFunc %s.%s\n", path, member_func);

        luaL_stack_dump(L);
        return ret;
    }

    /**
     * @param bPureFunc true: call Table.Func(...) , false: call Table:Func(...)
     */
    bool callMemberFuncV2(const char *path, const char *member_func, bool bPureFunc)
    {
        YALUA_LOG("-->>begin CallLuaMemberFuncV2 %s.%s", path, member_func);

        std::vector<std::string> result;
        result = ut::str::split(path, '.');

        bool bGetFunc = false;

        if (result.size() == 1) {
            lua_getglobal(L, path);
            // lua_pushvalue(L, -1);
            lua_pushstring(L, member_func);
            lua_gettable(L, -2);

            if (lua_isfunction(L, -1)) {
                bGetFunc = true;
            }
        }
        else {
            // get global table
            lua_getglobal(L, result[0].c_str());
            // lua_pushvalue(L, -1);

            // iterate to last table
            for (size_t i = 1; i < result.size(); ++i) {
                lua_pushstring(L, result[i].c_str());
                lua_gettable(L, -2);
                YALUA_LOG("lua_gettable %s", result[i].c_str());
                // lua_pushvalue(L, -1);
            }

            // get function
            lua_pushstring(L, member_func);
            lua_gettable(L, -2);
            if (lua_isfunction(L, -1)) {
                bGetFunc = true;
            }
        }

        bool ret = false;

        dump_stack();
        if (bGetFunc) {
            if (bPureFunc) {
                ret = call_luafunc_impl(L, 0, 0);
            }
            else {
                lua_pushvalue(L, -2);
                ret = call_luafunc_impl(L, 1, 0);
            }
        }
        else {
            YALUA_LOG("failed to get %s.%s\n", path, member_func);
        }

        YALUA_LOG("--<<end CallLuaMemberFuncV2 %s.%s\n", path, member_func);
        lua_pop(L, result.size());

        luaL_stack_dump(L);

        return ret;
    }

    bool LuaCall(int nargs, int nret)
    {
        return call_luafunc_impl(L, nargs, nret);
    }


    template <TLuaPushable T>
    void pushValue(T v)
    {
        push_value_1(std::forward<T>(v));
    }


    template <TLuaPushable... Args>
    bool callFunc(const char *func, Args... args)
    {
        YALUA_LOG("\n-->>CallLuaFunc %s\n", func);

        if (!get_global_func(func)) {
            return false;
        }
        push_value_sequence(std::forward<Args>(args)...);

        dump_stack();

        if (!call_luafunc_impl(L, sizeof...(args), 0)) {
            return false;
        }

        YALUA_LOG("--<<end pcall %s\n", func);
        return true;
    }

    void a()
    {
        callFunc("test_func", 123, "hello", true);
    }

    // bool callFunc(const char *func)
    // {
    //     YALUA_LOG("\n-->>CallLuaFunc %s\n", func);

    //     if (!get_global_func(func)) {
    //         return false;
    //     }

    //     dump_stack();

    //     if (!call_luafunc_impl(L, 0, 0)) {
    //         return false;
    //     }

    //     YALUA_LOG("--<<end pcall %s\n", func);
    //     return true;
    // }



    template <typename T, TLuaPushable... Args>
    bool callFuncWithRet(T &out, const char *func, Args... args)
    {
        YALUA_LOG("\n-->>CallLuaFuncWithRet %s\n", func);

        if (!get_global_func(func)) {
            return false;
        }

        push_value_1(L, std::forward<Args>(args)...);

        if (!call_luafunc_impl(L, sizeof...(args), 1)) {
            return false;
        }

        using t = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<t, float>) {
            out = lua_tonumber(L, -1);
            return true;
        }
        else if constexpr (std::is_same_v<t, int>) {
            out = lua_tointeger(L, -1);
            return true;
        }

        return false;
    }


    template <typename T>
    int createRef(T value)
    {
        pushValue(value);

        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (ref == LUA_REFNIL || ref == LUA_NOREF) {
            YALUA_LOG("create ref failed\n");
        }
        return ref;
    }


  private:


    bool get_global_func(const char *func)
    {
        int type = lua_getglobal(L, func);
        YALUA_LOG("lua_getglobal %s, Type: %s\n", func, lua_typename(L, -1));
        if (type != LUA_TFUNCTION) {
            YALUA_LOG("failed to get global %s\n", func);
            YALUA_LOG("%d %s\n", type, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
        return true;
    }

    template <TLuaPushable T>
    void push_value_1(T arg)
    {
        using Arg = std::remove_cv_t<std::remove_reference_t<T>>;

        if constexpr (std::is_floating_point_v<Arg>)
        {
            // now use float, maybe double in lua
            lua_pushnumber(L, float(arg));
            return;
        }
        if constexpr (std::is_integral_v<Arg>)
        {
            // now use int, maybe long long in lua
            lua_pushnumber(L, int(arg));
            return;
        }
        if constexpr (std::is_same_v<Arg, const char *>)
        {
            lua_pushstring(L, arg);
            return;
        }
        if constexpr (std::is_same_v<Arg, bool>)
        {
            lua_pushboolean(L, arg);
            return;
        }
        if constexpr (std::is_same_v<Arg, nullptr_t>)
        {
            lua_pushnil(L);
            return;
        }
        // Add more types as needed
        YALUA_LOG("Unknown type %s\n", typeid(arg).name());
        exit(-1);
    }


    // terminate recursion condition
    int push_value_sequence() { return 0; }

    template <TLuaPushable T, TLuaPushable... Ts>
    int push_value_sequence(T &&t, Ts &&...ts)
    {
        push_value_1(std::forward<T>(t));
        return 1 + push_value_sequence(std::forward<Ts>(ts)...);
    }

    bool call_luafunc_impl(lua_State *L, int nargs, int nret);
};
