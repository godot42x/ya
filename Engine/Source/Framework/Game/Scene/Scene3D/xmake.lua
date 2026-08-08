target("ya-scene-3d")
    set_kind("shared")
    ya_std_module("YA_SCENE_3D_API")
    add_includedirs("../../../..", { public = true })
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Node3D extends the GUI scene-tree base (Node) and reaches the ECS
    -- TransformComponent, so it depends on both product lines.
    add_deps("ya-gui-runtime", "ya-gameplay-ecs", "ya-foundation-core", { public = true })
    add_packages("glm", { public = true })
