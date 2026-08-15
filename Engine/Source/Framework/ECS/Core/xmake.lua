-- ECS core: lightweight ECS infrastructure (registry / entity / component
-- base / component mutation / scene bus) that any consumer can build against
-- without pulling in Game/Render/Host. Public API is exposed through the
-- module forwarding root (include/ECS/...); the original headers live next to
-- the sources. Scene-backed Entity semantics (rename / validity) are provided
-- by ya-scene-core through the entity-scene contract, so this module has no
-- Scene dependency edge.
target("ya-ecs-core")
    set_kind(ya_target_kind())
    ya_std_module("YA_ECS_CORE_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h", "*.inl")
    add_deps("ya-foundation-core", "reflects-core", { public = true })
    add_packages("entt", "nlohmann_json", { public = true })
