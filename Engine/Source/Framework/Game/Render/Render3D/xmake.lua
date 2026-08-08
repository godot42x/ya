target("ya-render-3d")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_3D_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    -- Render3D public headers consume the generated shader-interface headers
    -- (slang.h / glsl.h); the generated roots are propagated to consumers
    -- as public include dirs, scoped to this module only.
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/GLSL/Generated"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated/Common"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/GLSL/Generated/Common"), { public = true })
    -- Transition (Phase 7): 13 render-3d files still compile Host headers
    -- (app services). Private-only so the Product tier root never propagates
    -- to consumers; removed when the app-services interfaces land.
    add_includedirs(path.join(os.projectdir(), "Engine/Source/Product"))
    add_deps(
        "ya-gui-resources",
        "ya-gui-compose",
        "ya-resource-core", "ya-resource-loader", "ya-resource-runtime",
        "ya-render-graph",
        { public = true })
    add_deps("ya-ecs-core", "ya-gameplay-systems")
    -- Implementation-only deps: scene data/lifecycle, GUI resources/compose,
    -- physics debug lines and the backend builtin texture library.
    add_deps("ya-gui-resources", "ya-gui-compose", "ya-physics", "ya-rhi-backend-common", "ya-rhi-vulkan", "ya-scene-3d", "ya-scene-core", "ya-scene-runtime")
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
