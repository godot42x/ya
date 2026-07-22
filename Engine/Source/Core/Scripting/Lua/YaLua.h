#pragma once


#include "sol/sol.hpp"


struct LuaVar
{

    sol::state _lua;

    void callFunction(const std::string& functionName, const sol::variadic_args& args)
    {
        sol::function func = _lua[functionName];
        if (func.valid()) {
            func(args);
        }
        else {
            // Handle the case where the function is not found
            throw std::runtime_error("Function " + functionName + " not found in Lua state.");
        }
    }
};