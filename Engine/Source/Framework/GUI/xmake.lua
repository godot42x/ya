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
includes("./Tooling/xmake.lua")
includes("./App/xmake.lua")

-- GUI framework aggregate: the single link target for pure-GUI code. It
-- carries no sources of its own; public deps re-export the full closure
-- (foundation + RHI backend + the four GUI modules) to consumers. The
-- standalone native app host (window/SDL/present) is a separate library:
-- ya-gui-host. Executable consumers link that host directly.
target("ya-gui-framework")
    set_kind(ya_meta_kind())
    -- Single empty TU so the shared facade has a DLL entry point (the target
    -- itself carries no real sources; see Module.cpp).
    add_files("Module.cpp", { unity_ignored = true })
    add_deps(
        "ya-foundation-core",
        "ya-app-kernel",
        "ya-app-control",
        "ya-hierarchy",
        "ya-rhi",
        "ya-rhi-backend-common",
        "ya-rhi-vulkan",
        "ya-gui-resources",
        "ya-gui-draw2d",
        "ya-gui-widgets",
        "ya-gui-compose",
        "ya-gui-tooling",
        { public = true })
