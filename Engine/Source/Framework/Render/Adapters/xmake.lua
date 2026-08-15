-- Render ECS adapters: the bridge between ECS components and the Render3D
-- runtime. Holds render-side linkage rules (light billboards, material
-- topology) and will absorb the render-facing component resolve/binding logic
-- as the transitional fat ECS module dissolves.
target("ya-render-ecs-adapters")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_ECS_ADAPTERS_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    -- Rules operate on ECS components and schedule through the generic
    -- linkage framework; they never reach Host/App.
    add_deps("ya-foundation-core", "ya-component-linkage", { public = true })
    add_deps("ya-ecs-core", "ya-ecs-systems", "ya-scene-core")
    -- The bridge layer is allowed to reach the resource and Render3D layers
    -- (Phase 2 closure: ecs-core + Resource + Render3D); resolve/binding
    -- services land here as the fat ECS module dissolves.
    add_deps("ya-resource-core", "ya-resource-loader", "ya-resource-runtime", "ya-render-3d", "ya-scene-3d", "ya-scene-runtime")
    -- Model instantiation walks the scene tree base (Node) from ya-hierarchy.
    add_deps("ya-hierarchy")
    if is_plat("windows") then
        add_cxxflags("/bigobj")
    end
    add_packages("entt", "glm")
