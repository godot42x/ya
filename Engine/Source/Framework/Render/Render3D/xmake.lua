target("ya-render-3d")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_3D_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    -- Render3D public headers consume the generated Slang shader-interface
    -- headers; the generated roots are propagated to consumers as public
    -- include dirs, scoped to this module only.
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated"), { public = true })
    add_includedirs(path.join(os.projectdir(), "Engine/Shader/Slang/Generated/Common"), { public = true })
    add_deps(
        "ya-render-resources",
        "ya-gui-compose",
        "ya-resource-core", "ya-resource-loader", "ya-resource-runtime",
        "ya-render-graph",
        { public = true })
    add_deps("ya-ecs-core", "ya-ecs-systems")
    -- Implementation-only deps: scene data/lifecycle and the backend builtin
    -- texture library (GUI resources/compose are already public above: render-3d
    -- public headers expose them). No ya-physics: the physics collision debug
    -- draw is an ECS-to-render bridge and lives in ya-render-ecs-adapters.
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan", "ya-scene-3d", "ya-scene-core", "ya-scene-runtime")
    add_packages("glm", "entt", "nlohmann_json", { public = true })
    add_packages("cxxopts", "vulkan-memory-allocator", "glad", "lua", "sol2", "quickjs-ng", "vulkansdk", "stb")
    if is_plat("windows") then
        add_cxxflags("/bigobj")
    end
