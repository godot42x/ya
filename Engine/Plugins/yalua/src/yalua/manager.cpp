//
/*
 * @ Author: godot42
 * @ Create Time: 2025-01-11 04:04:25
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-05 01:46:13
 * @ Description:
 */

#include "core.h"

#include "machine.h"



LuaMachineManager &LuaMachineManager::get()
{
    static LuaMachineManager manager;
    return manager;
}

LuaMachine LuaMachineManager::NewMachine()
{
    lua_State *L = luaL_newstate();
    L2Idx.insert({L, Index});
    Idx2L.insert({Index, L});
    LuaMachine lm(L, Index);
    ++Index;

    return lm;
}

bool LuaMachineManager::RemoveMachine(lua_State *L)
{
    if (!L) {
        return false;
    }

    if (auto it = L2Idx.find(L); it != L2Idx.end()) {
        lua_State *L = it->first;
        Idx2L.erase(it->second);
        L2Idx.erase(it->first);
        lua_close(L);
        return true;
    }
    return false;
}

bool LuaMachineManager::RemoveMachine(LuaMachine &machine)
{
    return RemoveMachine(machine.L);
}



int32_t LuaMachineManager::GetIndex(lua_State *L)
{
    if (auto it = L2Idx.find(L); it != L2Idx.end()) {
        return it->second;
    }
    return -1;
}

LuaMachineManager::~LuaMachineManager()
{
    for (auto L : L2Idx)
    {
        lua_close(L.first);
    }
}
