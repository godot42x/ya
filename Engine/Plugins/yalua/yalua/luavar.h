//
/*
 * @ Author: godot42
 * @ Create Time: 2025-01-06 20:30:02
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-04 01:36:13
 * @ Description:
 */



#pragma once

#include "lauxlib.h"
extern "C" {
#include "lua.h"
}
#include "core.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <variant>

#include "manager.h"

#include "utility.cc/string_utils.h"

#include "log.h"



struct LuaVar
{


    std::variant<lua_Integer,
                 lua_Number,
                 std::string,
                 void *, // userdata/table
                 bool>
                value;
    ELuaType::T type;

    LuaVar() { type = ELuaType::Nil; }


    static LuaVar fromRef(int ref)
    {
        // TODO: check ref valid?
        LuaVar ret;
        ret.type  = ELuaType::Ref;
        ret.value = ref;
        return ret;
    }

    void SetValue(lua_State *L, int pos)
    {
        type = (ELuaType::T)lua_type(L, pos);
        YALUA_LOG("set value type of pos {}: {} -> {}", pos, (int)type, lua_typename(L, type));

        switch (type) {
        case LUA_TNIL:
        {
            type = ELuaType::Nil;
            break;
        }
        case LUA_TSTRING:
        {
            // TODO: string ref?
            size_t len;
            value.emplace<std::string>(lua_tolstring(L, -1, &len), len);
            type = ELuaType::String;
            break;
        }
        case LUA_TFUNCTION:
        {
            type = ELuaType::Function;
        }

        case LUA_TTABLE:
        {
            type = ELuaType::Table;
        }
        case LUA_TUSERDATA:
        {
            type = ELuaType::UserData;
            break;
        }
        case LUA_TLIGHTUSERDATA:
        {
            type  = ELuaType::LightUserData;
            value = lua_touserdata(L, pos);
            break;
        }

        case LUA_TBOOLEAN:
        {
            value = lua_toboolean(L, pos);
            type  = ELuaType::Boolean;
            break;
        }
        case LUA_TNUMBER:
        {
            if (lua_isinteger(L, pos)) {
                value = lua_tointeger(L, pos);
                type  = ELuaType::Integer;
            }
            else {
                value = lua_tonumber(L, pos);
                type  = ELuaType::Number;
            }
            break;
        }
        default:
            type = ELuaType::Nil;
            break;
        }


        return;
    }

    LuaVar Get(lua_State *L, const std::string &key)
    {
        YALUA_LOG("index {}, cur luavar type: {}", key.data(), (int)type);
        if (type == ELuaType::Table) {
            YALUA_LOG("get table %s", key.data());
            lua_pushstring(L, key.c_str());
            int w = lua_gettable(L, -2);
            YALUA_LOG("w %d", w);
        }
        else if (type == ELuaType::Nil) {
            YALUA_LOG("get nil, get global %s", key.data());
            lua_getglobal(L, key.data());
        }

        SetValue(L, -1);
        // lua_pushvalue(L, std::get<LuaRef>(ret.value).pos);

        return *this;
    }


    static LuaVar GetValue(lua_State *L, std::string_view path)
    {
        LuaVar ret;

        // push global table
        // lua_pushglobaltable(L);
        std::string      left;
        std::string_view right;

        bool bFound = false;

        do {
            bFound = ut::str::split(path, '.', left, right);
            YALUA_LOG("left %s, right %s", left.data(), right.data());
            ret = ret.Get(L, left);
            luaL_stack_dump(L);
            YALUA_LOG("LuaVar::Get ret type: {}", (int)ret.type);
            if (ret.type != ELuaType::Table) {
                break;
            }
            if (right.empty()) {
                break;
            }
            path = right;
        } while (bFound);

        // lua_pop(L, 1);
        return ret;
    }
};