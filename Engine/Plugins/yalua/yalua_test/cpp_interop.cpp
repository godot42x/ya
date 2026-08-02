
#include "yalua/yalua.h"
#include "gtest/gtest.h"

TEST(yalua_cpp_interop, test_basic_interop_between_cpp_and_lua)
{
    LuaMachine lm = LuaMachineManager::get().NewMachine();
    lua_State *L  = lm.L;
    luaL_openlibs(L);

}