target("ya-scene-3d")
    set_kind("shared")
    ya_std_module("YA_SCENE_3D_API")
    ya_tier_include("Scene")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Node3D extends the GUI scene-tree base (Node); TransformComponent and
    -- ManagedChildComponent (3D scene data) live here with it. ECS reach is
    -- the core infrastructure only (no gameplay dependency).
    add_deps("ya-gui-runtime", "ya-ecs-core", "ya-foundation-core", { public = true })
