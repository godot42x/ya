

#include "yalua/core.h"
#include "yalua/machine.h"
#include "yalua/manager.h"
#include "gtest/gtest.h"



#include "yalua/luavar.h"


TEST(yalua, create_ref_with_stack_size)
{
    LuaMachine lm = LuaMachineManager::get().NewMachine();
    lua_State *L  = lm.L;
    luaL_openlibs(L);

    lua_newtable(L);
    int n = lua_gettop(L);
    debug(), "stack size before create ref: ", n, "\n";

    int r = lm.createRef(42);
    LuaVar::fromRef(r);


    n = lua_gettop(L);
    debug(), "stack size after create ref: ", n, "\n";
}