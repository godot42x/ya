-- Component linkage framework: scene lifecycle hook + component signal
-- dispatch + deferred frame-task scheduling for linkage rules. Owns no
-- business state; rules (light billboards, material topology, ...) are
-- registered by the Host composition and live in the render adapters.
target("ya-component-linkage")
    set_kind(ya_target_kind())
    ya_std_module("YA_COMPONENT_LINKAGE_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", { public = true })
    -- Linkage rules operate on ECS entities and scene lifecycle; private.
    add_deps("ya-ecs-core", "ya-scene-core", "ya-scene-runtime")
    add_packages("entt")
