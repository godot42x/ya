target("ya-render-3d")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_3D_API")
    -- Transition: 14 render-3d files still compile Host headers (app
    -- services); replaced by injected service contracts (Phase 7).
    ya_tier_include("Render", "Product")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Render3D public headers consume the generated shader-interface headers
    -- (slang.h / glsl.h); the generated roots are propagated to consumers
    -- as public include dirs, scoped to this module only.
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/GLSL/Generated"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated/Common"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/GLSL/Generated/Common"), { public = true })
    add_deps(
        "ya-gui-resources",
        "ya-gui-compose",
        "ya-resource-core", "ya-resource-loader", "ya-resource-runtime",
        "ya-render-graph",
        "ya-scene-3d",
        "ya-gameplay-ecs",
        "ya-gameplay-systems",
        "ya-component-linkage",
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
