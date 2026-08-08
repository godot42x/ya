-- GUI scene: Node2D/UIBase and the UI scene traversal. The scene-tree base
-- (Node) comes from the renderer-independent ya-hierarchy module.
target("ya-gui-scene")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-hierarchy", { public = true })
    add_deps("ya-gui-draw2d", "ya-gui-resources")
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("glm", { public = true })
