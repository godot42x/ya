-- do
--     IsHacked = IsHacked or false
--     local old_print = print
--     local old_cprint = cprint
--     if not IsHacked then
--         IsHacked = true
--         print("HACK IT")
--         print = function(...)
--             old_print(os.scriptdir(),"|", ...)
--         end

--         cprint = function(...)
--             old_cprint(os.scriptdir(), "|", ...)
--         end
--         print("HACK IT END")
--     end
-- end

-- print("1")

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
