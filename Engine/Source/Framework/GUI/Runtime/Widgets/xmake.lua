-- GUI widgets: the Game UI live visual tree (ui-widget-tree-refactor Phase 1).
--   UIElement      - widget base class (layout/paint/input; NO Node/Scene/ECS)
--   WidgetTree     - single live visual tree: internal root, system layers,
--                    attach/reparent/detach, layout/hit/focus/capture
--   WidgetAttachment - detach handle returned by attach*
--   UITypeRegistry - stable type IDs, explicit registration, module
--                    owner + live-instance unload guard
--   UIDocument     - reusable `.yaui` authoring data (detached subtree)
--   Controls/      - the basic widgets (Panel/Text/Button/Container)
--
-- Boundary: must never depend on Scene/ECS/Render3D/Host/Editor. Paint
-- records through the GUI Draw2D batch (ya-gui-draw2d) and the font atlas
-- (ya-gui-resources); both are inside the GUI closure.
target("ya-gui-widgets")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_includedirs("../Layout/include", { public = true })
    add_files("*.cpp", "Controls/*.cpp", "../Layout/*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h", "Controls/*.h", "../Layout/*.h", "../Layout/include/**.h")
    add_deps("ya-foundation-core", { public = true })
    add_deps("ya-gui-resources")
    add_packages("glm", { public = true })
