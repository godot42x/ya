-- Render ECS adapters: the bridge between ECS components and the Render3D
-- runtime. Holds render-side linkage rules (light billboards, material
-- topology) and will absorb the render-facing component resolve/binding logic
-- as the transitional fat ECS module dissolves.
target("ya-render-ecs-adapters")
    set_kind("shared")
    ya_std_module("YA_RENDER_ECS_ADAPTERS_API")
    ya_tier_include("Render")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Rules operate on ECS components (transitional fat module) and schedule
    -- through the generic linkage framework; they never reach Host/App.
    add_deps(
        "ya-foundation-core",
        "ya-ecs-core",
        "ya-gameplay-ecs",
        "ya-gameplay-systems",
        "ya-component-linkage",
        "ya-scene-core",
        { public = true })
    add_packages("entt", "glm")
