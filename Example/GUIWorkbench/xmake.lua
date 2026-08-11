-- GUIWorkbench: the product-level probe for the GUI framework (Phase 1 shell,
-- Phase 3 = real tool workspace). A standalone binary consuming the same
-- ya-gui-app-host library as GUIFrameworkSmoke; it never copies the
-- SDL/Vulkan frame loop and never depends on Scene/ECS/Render3D/Host/Editor.
target("GUIWorkbench")
    set_kind("binary")
    add_files("./Source/**.cpp")
    add_deps("ya-gui-app-host")
