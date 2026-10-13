-- ya-physics-2d: 2D rigid bodies and triggers on Box2D, for the top-down 2D
-- gameplay line. Separate from ya-physics (Jolt, 3D): a 2D game wants AABB /
-- circle colliders, sensors and contact events in a plane, which Jolt's 3D
-- box/sphere does not cover.
--
-- The system writes a body's x/y back into the entity's TransformComponent and
-- leaves z alone, so a body still lives on the same Node3D + Transform as every
-- other scene object (there is no Transform2D).
target("ya-physics-2d")
    set_kind(ya_target_kind())
    ya_std_module("YA_PHYSICS_2D_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-ecs-core", { public = true })
    -- The system reaches the active scene and writes back TransformComponents.
    add_deps("ya-scene-core", "ya-scene-runtime")
    add_deps("ya-scene-3d")
    add_packages("box2d", "glm")
