// GridMover's pure-logic cases (Engine/Test/Lua/GridMoverTest.lua) under the engine's Lua 5.4.

#include "TestSource.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <sol/sol.hpp>
#include <string>

namespace ya
{
namespace
{

TEST(GridMoverLuaTest, CasesPassUnderLua54)
{
    const std::filesystem::path root = test::repoRoot();
    ASSERT_FALSE(root.empty()) << "repo root not found from " << __FILE__;
    const std::filesystem::path script = root / "Engine/Test/Lua/GridMoverTest.lua";
    ASSERT_TRUE(std::filesystem::exists(script)) << script;

    sol::state lua;
    lua.open_libraries(sol::lib::base,
                       sol::lib::package,
                       sol::lib::string,
                       sol::lib::os,
                       sol::lib::math,
                       sol::lib::table,
                       sol::lib::io);
    // The file calls os.exit(1) when an assertion fails. That must not kill
    // the test process.
    lua.script(R"lua(
        os.exit = function(code)
            error("GridMoverTest failed: os.exit(" .. tostring(code) .. ")", 0)
        end
    )lua");
    lua["YA_GRID_MOVER_SCRIPT"] = script.generic_string();
    lua.script("arg = { [0] = YA_GRID_MOVER_SCRIPT }");

    const sol::protected_function_result result = lua.safe_script_file(script.generic_string(), sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

} // namespace
} // namespace ya
