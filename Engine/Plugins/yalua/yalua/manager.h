//
/*
 * @ Author: godot42
 * @ Create Time: 2025-01-06 20:33:36
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-02 19:03:58
 * @ Description:
 */



#pragma once

#include <atomic>
#include <cstdint>
#include <map>


#include "core.h"

struct YALUA_API LuaMachineManager
{

    std::map<lua_State *, int32_t> L2Idx;
    std::map<int32_t, lua_State *> Idx2L;
    std::atomic<int32_t>           Index = 0;


    static LuaMachineManager &get();

    LuaMachine NewMachine();
    bool       RemoveMachine(lua_State *L);
    bool       RemoveMachine(LuaMachine &machine);
    int32_t    GetIndex(lua_State *L);

  private:
    LuaMachineManager() = default;
    ~LuaMachineManager();
    LuaMachineManager(const LuaMachineManager &)            = delete;
    LuaMachineManager(LuaMachineManager &&)                 = delete;
    LuaMachineManager &operator=(const LuaMachineManager &) = delete;
    LuaMachineManager &operator=(LuaMachineManager &&)      = delete;
};