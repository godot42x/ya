-- Gameplay systems: frame-driven gameplay logic on top of the ECS core and
-- the scene line. Systems never reach Host/Render3D; scene access is injected
-- through narrow provider seams by the Host composition.
target("ya-gameplay-systems")
    set_kind("shared")
    ya_std_module("YA_GAMEPLAY_SYSTEMS_API")
    ya_tier_include("Gameplay")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-scene-core", "ya-scene-3d", { public = true })
    add_packages("quickjs-ng", "nlohmann_json", "entt", "glm")
