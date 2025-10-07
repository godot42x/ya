//
/*
 * @ Author: godot42
 * @ Create Time: 2025-01-11 05:31:16
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-04 02:10:01
 * @ Description:
 */

#include "core.h"


void luaL_stack_dump(lua_State *L)
{
    // print a specific color
    printf("\x1b[32m");

    printf("***** begin stack dump **\n");
    int n = lua_gettop(L);
    printf("stack size: %d\n", n);
    for (int i = 1; i <= n; ++i) {
        int type = lua_type(L, i);
        switch (type) {
        case LUA_TSTRING:
            printf("str:[%s]", lua_tostring(L, i));
            break;
        case LUA_TBOOLEAN:
            printf("[%s]", lua_toboolean(L, i) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            printf("[%g]", lua_tonumber(L, i));
            break;
        case LUA_TFUNCTION:
            printf("[%s]", lua_tostring(L, i));
            break;
        default:
            printf("[%s]", lua_typename(L, type));
            break;
        }
        printf(", ");
    }
    printf("\n***** end stack dump **\n");
    // end color
    printf("\x1b[0m");
}