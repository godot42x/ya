target("ya-render-3d")
    set_kind("shared")
    ya_std_module("YA_RENDER_3D_API")
    ya_tier_include("Render")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps(
        "ya-gui-runtime",
        "ya-resource",
        "ya-render-graph",
        "ya-scene-3d",
        "ya-gameplay-ecs",
        "ya-gameplay-systems",
        "ya-physics",
        { public = true })
    -- Scene data/lifecycle types are consumed by Render3D implementation
    -- .cpp files only (RenderRuntime pipelines, overlays); Render3D public
    -- headers do not expose them, so the deps stay private.
    add_deps("ya-scene-core", "ya-scene-runtime")
    -- Transition: render-3d still compiles host-layer headers (App services).
    -- Planned decoupling: app-service interface injection (see plan.md §10).
    ya_engine_defines()
    add_packages("glm", "entt", "nlohmann_json", { public = true })
    add_packages("cxxopts", "vulkan-memory-allocator", "glad", "lua", "sol2", "quickjs-ng", "vulkansdk", "stb")
    if is_plat("macosx") then
        -- Transition: render-3d calls host App services (see plan.md §10);
        -- symbols resolve from the final binary until the app-services
        -- interface lands.
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end
