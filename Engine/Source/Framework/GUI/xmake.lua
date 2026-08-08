-- GUI framework product line, split by stable responsibility:
--   ya-gui-resources  font/glyph + texture-slot binding
--   ya-gui-draw2d     sprite/text/line batching + 2D pipeline
--   ya-gui-scene      Node2D/UIBase + UI scene traversal
--   ya-gui-compose    viewport/UI compose pass
-- Widgets (panels/buttons/...) become their own module once extracted.

includes("./Runtime/Resource/xmake.lua")
includes("./Runtime/Draw2D/xmake.lua")
includes("./Runtime/Scene/xmake.lua")
includes("./Runtime/Compose/xmake.lua")

-- GUI framework aggregate: the single link target for pure-GUI hosts. It
-- carries no sources of its own; public deps re-export the full closure
-- (foundation + RHI backend + the four GUI modules) to consumers.
target("ya-gui-framework")
    set_kind("shared")
    add_deps(
        "ya-foundation-core",
        "ya-hierarchy",
        "ya-rhi",
        "ya-rhi-backend-common",
        "ya-rhi-vulkan",
        "ya-gui-resources",
        "ya-gui-draw2d",
        "ya-gui-scene",
        "ya-gui-compose",
        { public = true })
