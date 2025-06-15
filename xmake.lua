-- function include_project(name)
--     includes(name .. "/src")
-- end

-- set_targetdir("./bin")


add_rules("mode.debug", "mode.release")

set_languages("c++20")

if is_plat("windows") then
    set_exceptions("cxx")
end

--includes("engine")

includes("./xmake/rule.lua")
includes("./xmake/package/xmake.lua")
includes("./Engine/Neon.xmake.lua")

includes("./xmake/task.lua")

set_rundir(os.scriptdir())
