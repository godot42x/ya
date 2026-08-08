target("ya-scene-3d")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCENE_3D_API")
    ya_tier_include("Scene")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Node3D extends the renderer-independent scene-tree base (Node);
    -- TransformComponent and ManagedChildComponent (3D scene data) live here
    -- with it. ECS reach is the core infrastructure only (no gameplay
    -- dependency). The scene tree base lives in ya-hierarchy, so the 3D
    -- scene line no longer reaches the GUI modules.
    add_deps("ya-hierarchy", "ya-ecs-core", "ya-foundation-core", { public = true })
