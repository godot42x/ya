-- GUI host: standalone native GUI app lifecycle for GUI-only binaries.
-- Owns the native window / SDL event source / presentation target / frame loop
-- glue that sits above the AppKernel loop and the GUI runtime modules. This is
-- the single GUI window/bootstrap/host owner (AppRuntime + GUI/App merged);
-- it must never depend on Scene/ECS/Render3D/Product Host/Editor.
target("ya-gui-host")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-rhi", "ya-app-kernel", "ya-app-control", { public = true })
    add_deps("ya-gui-resources", "ya-gui-draw2d", "ya-gui-widgets", "ya-gui-compose", { public = true })
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("libsdl3")
    add_packages("glm", { public = true })
