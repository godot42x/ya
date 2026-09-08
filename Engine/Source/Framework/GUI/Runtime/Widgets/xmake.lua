-- GUI widgets: the Game UI live visual tree (ui-widget-tree-refactor Phase 1).
--   UIElement      - widget base class (layout/paint/input; NO Node/Scene/ECS)
--   WidgetTree     - single live visual tree: internal root, system layers,
--                    attach/reparent/detach, layout/hit/focus/capture
--   WidgetAttachment - detach handle returned by attach*
--   UITypeRegistry - stable type IDs, explicit registration, module
--                    owner + live-instance unload guard
--   UIDocument     - reusable authoring data (detached subtree)
--   Controls/      - the basic widgets (Panel/Text/Button/Container)
--   Controls/DockSpace/ - dock session + docked/floating projections
--
-- Boundary: must never depend on Scene/ECS/Render3D/Host/Editor. Paint
-- records through the GUI Draw2D batch (ya-render-2d) and the font atlas
-- (ya-render-resources); both are inside the GUI closure.
target("ya-gui-widgets")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_includedirs("../Binding/include", { public = true })
    add_includedirs("../Layout/include", { public = true })
    add_includedirs("../Declarative/include", { public = true })
    add_files("*.cpp", "Controls/**.cpp|Controls/DockSpace/DockFloatingWindow.cpp", "../Layout/*.cpp")
    add_files("Controls/DockSpace/DockFloatingWindow.cpp", {unity_ignored = true})
    add_files("../Binding/*.cpp")
    add_files("../Declarative/*.cpp")
    add_headerfiles("./include/**.h", "../Binding/include/**.h", { public = true })
    add_headerfiles("*.h", "Controls/**.h", "../Binding/*.h", "../Layout/*.h", "../Layout/include/**.h", "../Declarative/include/**.h")
    add_deps("ya-foundation-core", { public = true })
    add_deps("ya-render-resources")
    add_packages("glm", { public = true })
