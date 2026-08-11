-- GUI framework product line, split by stable responsibility:
--   ya-gui-resources  font/glyph + texture-slot binding
--   ya-gui-draw2d     sprite/text/line batching + 2D pipeline
--   ya-gui-widgets    UIElement/WidgetTree/UITypeRegistry/UIDocument +
--                     snapshot builder + basic controls
--   ya-gui-compose    viewport/UI compose pass (consumes UIFrameSnapshot)

includes("./Runtime/Resource/xmake.lua")
includes("./Runtime/Draw2D/xmake.lua")
includes("./Runtime/Widgets/xmake.lua")
includes("./Runtime/Compose/xmake.lua")
includes("./App/xmake.lua")

-- GUI framework aggregate: the single link target for pure-GUI code. It
-- carries no sources of its own; public deps re-export the full closure
-- (foundation + RHI backend + the four GUI modules) to consumers. The
-- standalone native app host (window/SDL/present) is a separate library:
-- ya-gui-app-host. Executable consumers link that host directly.
target("ya-gui-framework")
    set_kind(ya_meta_kind())
    add_deps(
        "ya-foundation-core",
        "ya-hierarchy",
        "ya-rhi",
        "ya-rhi-backend-common",
        "ya-rhi-vulkan",
        "ya-gui-resources",
        "ya-gui-draw2d",
        "ya-gui-widgets",
        "ya-gui-compose",
        { public = true })
