target("ya-physics")
    set_kind("shared")
    ya_std_module("YA_PHYSICS_API")
    ya_tier_include("Game")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-gameplay-ecs", { public = true })
    -- PhysicsSystem reaches the active scene/SceneManager from its .cpp only.
    add_deps("ya-scene-core", "ya-scene-runtime")
    -- Transition: physics still compiles render/host headers (Scene, AppState).
    -- Planned decoupling: see plan.md §10.
    ya_engine_defines()
    add_packages("joltphysics", "glm")
    if is_plat("macosx") then
        -- Transition: physics calls host App services (see plan.md §10).
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end
