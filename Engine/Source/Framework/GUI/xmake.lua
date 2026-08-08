-- GUI framework product line. One runtime module covering the full 2D UI
-- stack: Draw2D batching, font/texture resources, the Node2D scene tree
-- (including the shared `Node` hierarchy base) and the viewport compose pass.
-- Widgets (panels/buttons/... ) become their own module here once extracted.
ya_module("ya-gui-runtime", "GUI_RUNTIME", {
    include_root = "../..",
    deps = {
        "ya-foundation-core",
        "ya-foundation-rhi",
        -- Phase-2 decoupling targets: FontManager/TextureLibrary currently
        -- reach ResourceRegistry/AssetManager, Node.cpp reaches Entity.name.
        -- Mapped honestly today so the GUI closure work is driven by the
        -- linker (see .agent/plan/gui-framework-module-split).
        "ya-resource",
        "ya-gameplay-ecs",
    },
    packages = {
        "freetype",
        "glm",
    },
})
