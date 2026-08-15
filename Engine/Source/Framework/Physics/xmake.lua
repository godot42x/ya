target("ya-physics")
    set_kind(ya_target_kind())
    ya_std_module("YA_PHYSICS_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-ecs-core", { public = true })
    -- PhysicsSystem reaches the active scene/SceneManager from its .cpp only.
    add_deps("ya-scene-core", "ya-scene-runtime")
    -- PhysicsSystem consumes Node3D/TransformComponent from the scene line.
    add_deps("ya-scene-3d")
    add_packages("joltphysics", "glm")
