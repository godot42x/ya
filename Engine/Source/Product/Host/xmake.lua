target("ya-host")
    set_kind("shared")
    ya_std_module("YA_HOST_API")
    ya_tier_include("Product")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-render-3d", "imgui-local", "imguizmo-local", { public = true })
    -- Host drives the UI scene (UISceneRenderer) and GUI fonts directly.
    add_deps("ya-gui-resources", "ya-gui-scene")
    -- Host binds the scene lifecycle sink and drives Scene/SceneManager from
    -- its own TUs; public headers only forward-declare scene types.
    add_deps("ya-scene-core", "ya-scene-runtime")
    -- Host composes the render ECS adapters (linkage rules).
    add_deps("ya-render-ecs-adapters")
    add_packages("libsdl3", "glm", "nlohmann_json", "cxxopts", { public = true })
    add_packages("asio", "vulkan-memory-allocator", "glad", "lua", "sol2", "quickjs-ng", "vulkansdk", "stb")
