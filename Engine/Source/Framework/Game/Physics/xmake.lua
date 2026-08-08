target("ya-physics")
    set_kind("shared")
    ya_std_module("YA_PHYSICS_API")
    add_includedirs("../../..", { public = true })
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-gameplay-ecs", { public = true })
    -- Transition: physics still compiles render/host headers (Scene, AppState).
    -- Planned decoupling: see plan.md §10.
    ya_engine_defines()
    add_packages("joltphysics", "glm", { public = true })
    if is_plat("macosx") then
        -- Transition: physics calls host App services (see plan.md §10).
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end
