-- GUIFrameworkSmoke: the minimal end-to-end standalone GUI smoke (formerly
-- Engine/Source/Framework/GUI/MinimalHost). It is the regression + GPU
-- lifecycle probe for the GUI product line, not the place where real tool
-- functionality grows:
--
--   SDL input  -> WidgetTree event routing (hover / press / click)
--   WidgetTree -> layout + immutable UIFrameSnapshot
--   snapshot   -> Render2D compose pass onto the swapchain presentation target
--
-- Links only the GUI closure through ya-gui-app-host (Core/RHI/Vulkan
-- backend + the four GUI modules + the standalone host); no
-- ECS/Physics/Resource/RenderGraph/Render3D/Host/Editor.
target("ya-gui-minimal-host")
    set_kind("binary")
    add_files("./Source/**.cpp")
    add_deps("ya-gui-app-host")
