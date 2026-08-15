-- Gameplay systems: frame-driven gameplay logic on top of the ECS core and
-- the scene line. Systems never reach Host/Render3D; scene access is injected
-- through narrow provider seams by the Host composition.
target("ya-ecs-systems")
    set_kind(ya_target_kind())
    ya_std_module("YA_ECS_SYSTEMS_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    -- Public headers reach Resource types (SkeletonAnimatorComponent holds
    -- skeleton data); ECS + scene line for the systems.
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-resource-core", "ya-scene-3d", { public = true })
    -- Implementation-only deps: resource runtime for model data, scene data
    -- and the scene-tree base (Node) for TransformSystem.
    add_deps("ya-resource-loader", "ya-resource-runtime", "ya-scene-core", "ya-hierarchy")
    -- Lua scripting (component + system) and JS scripting.
    add_packages("quickjs-ng", "nlohmann_json", "entt", "glm", "sol2", { public = true })
    add_packages("lua")
