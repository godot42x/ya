-- GUI framework product line. One runtime module covering the full 2D UI
-- stack: Draw2D batching, font/texture resources, the Node2D scene tree
-- (including the shared `Node` hierarchy base) and the viewport compose pass.
-- Widgets (panels/buttons/... ) become their own module here once extracted.
target("ya-gui-runtime")
    set_kind("shared")
    ya_std_module("YA_GUI_API")
    ya_tier_include("Framework")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- The 2D renderer instantiates descriptor pools / pipeline layouts via
    -- the RHI factory entry points implemented by the backend, so the GUI
    -- closure includes the platform backend (Foundation tier, per plan).
    -- Node2D/UI scene extend the renderer-independent scene-tree base.
    add_deps("ya-foundation-core", "ya-hierarchy", "ya-rhi", "ya-rhi-backend-common", "ya-rhi-vulkan", { public = true })
    add_packages("glm", { public = true })
    add_packages("freetype")

-- GUI framework aggregate: the single link target for pure-GUI hosts. It
-- carries no sources of its own; public deps re-export the full closure
-- (foundation + RHI backend + GUI runtime) to consumers.
target("ya-gui-framework")
    set_kind("shared")
    add_deps("ya-foundation-core", "ya-rhi", "ya-rhi-backend-common", "ya-rhi-vulkan", "ya-gui-runtime", { public = true })
