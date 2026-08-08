-- Gameplay animation: skeleton pose sampling driven by the ECS animator
-- component. The active scene and the tick policy (world render enabled) are
-- injected by the Host; this module never reaches Host or RenderRuntime.
target("ya-gameplay-animation")
    set_kind("shared")
    ya_std_module("YA_GAMEPLAY_ANIMATION_API")
    ya_tier_include("Gameplay", "Scene")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", { public = true })
    -- Implementation-only reach (private): ECS animator component, skeleton
    -- sampling resources and the scene data structure.
    add_deps("ya-gameplay-ecs", "ya-resource", "ya-scene-core")
