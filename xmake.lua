add_rules("mode.debug", "mode.release")
set_languages("c++20")

if is_plat("windows") then
    set_exceptions("cxx")
    set_runtimes("MT") -- use static link and no debug runtime library
end


includes("./xmake/rule.lua")
includes("./xmake/package/xmake.lua")
includes("./Engine/Neon.xmake.lua")

includes("./xmake/task.lua")

set_rundir(os.scriptdir())
