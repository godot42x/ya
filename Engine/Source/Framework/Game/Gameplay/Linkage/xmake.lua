-- Component linkage framework: scene lifecycle hook + component signal
-- dispatch + deferred frame-task scheduling for linkage rules. Owns no
-- business state; rules (light billboards, material topology, ...) are
-- registered by the Host composition and live in the render adapters.
target("ya-component-linkage")
    set_kind(ya_target_kind())
    ya_std_module("YA_COMPONENT_LINKAGE_API")
    ya_tier_include("Gameplay")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-scene-core", "ya-scene-runtime", { public = true })
    add_packages("entt")
