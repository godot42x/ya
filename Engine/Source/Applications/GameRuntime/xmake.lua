target("ya-game-runtime")
    set_kind(ya_target_kind())
    ya_std_module("YA_GAME_RUNTIME_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-gui-host")
    -- AppOptions.h publicly exposes shared control-plane CLI parsing helpers,
    -- so runtime consumers must see ya-app-control directly instead of only
    -- via transitive host dependencies.
    add_deps("ya-app-control", { public = true })
    -- App.h publicly exposes IModule (addModule), so the module-system target
    -- is a public dependency of the runtime shell.
    add_deps("ya-module-manager", { public = true })
    add_deps("ya-render-3d", "imgui-local", "imguizmo-local", { public = true })
    -- Host drives GUI fonts directly; Game UI lives in the widgets module.
    add_deps("ya-render-resources", "ya-gui-widgets")
    -- Host binds the scene lifecycle sink and drives Scene/SceneManager from
    -- its own TUs; public headers only forward-declare scene types.
    add_deps("ya-scene-core", "ya-scene-runtime")
    -- Host composes the render ECS adapters (linkage rules).
    add_deps("ya-render-ecs-adapters")
    -- Host TUs use the Vulkan backend types directly (ImGui backend,
    -- screenshot readback, frame loop); the include root is exposed by
    -- ya-rhi-vulkan (backend-common no longer re-exports it).
    add_deps("ya-rhi-vulkan")
    add_packages("libsdl3", "glm", "nlohmann_json", "cxxopts", { public = true })
    add_packages("vulkan-memory-allocator", "glad", "lua", "sol2", "quickjs-ng", "vulkansdk", "stb")
