-- GUI app host: standalone native GUI app lifecycle for GUI-only binaries
-- (gui-app-bootstrap Phase 1).
--   GUIAppHost            - window / IRender / SDL input pump / frame loop /
--                           presentation target management / shutdown
--   GUIPresentationTarget - imported swapchain image wrapped as a
--                           GUIRenderSurface
--
-- Public surface exposes only the WidgetTree + IGUIAppDelegate contract:
-- no command buffer / swapchain / Vulkan type reaches app delegates.
-- Boundary: depends only on the GUI closure (Core/RHI/Vulkan backend + the
-- four GUI modules); never Scene/ECS/Render3D/Product Host/Editor.
target("ya-gui-app-host")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", "ya-app-runtime", { public = true })
    add_deps("ya-gui-resources", "ya-gui-draw2d", "ya-gui-widgets", "ya-gui-compose", { public = true })
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("libsdl3")
    add_packages("glm", { public = true })
