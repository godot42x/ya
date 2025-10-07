
#include <gtest/gtest.h>

#include <filesystem>

#include "yalua/core.h"
#include "yalua/log.h"



TEST(yalua, basic_usage)
{
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);
    // system("dir");
    // auto cur_dir = std::filesystem::current_path();
    // log("cur dir: %s", cur_dir.string().c_str());
    ASSERT_TRUE(std::filesystem::exists("./basic.lua"));
    luaL_loadfile(L, "./basic.lua");
    int n = lua_gettop(L);
    YALUA_LOG("stack size: {}", n);

    int r = lua_pcall(L, 0, 0, 0);
    YALUA_LOG("r {}", r);
}
