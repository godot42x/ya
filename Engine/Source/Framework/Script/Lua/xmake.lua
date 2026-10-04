-- Lua language backend. Not part of the ya-engine aggregate: only targets that
-- add_deps("ya-script-lua") see sol headers. ECS stays sol-free.
target("ya-script-lua")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCRIPT_LUA_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_deps("ya-foundation-core", "ya-ecs-systems", "ya-scene-core")
    add_packages("sol2", "lua", { public = true })
