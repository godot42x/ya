add_rules("mode.debug", "mode.release")
set_languages("c++20")

if is_plat("windows") then
    set_exceptions("cxx")
    set_runtimes("MT") -- use static link and no debug runtime library
end


set_policy("build.warning", true)
set_warnings("all", "extra")
if is_plat("windows") then
    if is_mode("debug") then
        add_cxflags(
            "/wd4251"   --  needs to have dll-interface to be used by clients of class
            , "/wd4100" --  unreferenced formal parameter
            , "/wd4267" --  conversion from 'size_t' to 'type', possible loss of data
            , "/wd4819" --  character that cannot be represented in the current code page
        )
        add_ldflags(
        -- "/ignore:4099" -- PDB not found
        )
    end
end

if is_mode("debug") then
    add_defines("BUILD_DEBUG")
else
    add_defines("BUILD_NO_DEBUG")
end


includes("./Xmake/Rule.lua")
includes("./Xmake/package/xmake.lua")
includes("./Engine/YA.xmake.lua")
includes("./Xmake/task.lua")
includes("./Test/xmake.lua")

-- add_rules("SourceFiles")

set_rundir(os.scriptdir())


add_rules("plugin.compile_commands.autoupdate", { outputdir = os.scriptdir() })

includes("./Example/Sandbox/xmake.lua")
includes("./Example/xmake.lua")
