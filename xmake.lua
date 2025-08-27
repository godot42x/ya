add_rules("mode.debug", "mode.release")
set_languages("c++20")

if is_plat("windows") then
    set_exceptions("cxx")
    set_runtimes("MT") -- use static link and no debug runtime library
end


set_policy("build.warning", true)
set_warnings("all", "extra")


includes("./Xmake/Rule.lua")
includes("./Xmake/package/xmake.lua")
includes("./Engine/YA.xmake.lua")
includes("./Xmake/task.lua")
includes("./Test/xmake.lua")

-- add_rules("SourceFiles")

set_rundir(os.scriptdir())


add_rules("plugin.compile_commands.autoupdate", { outputdir = os.scriptdir() })
