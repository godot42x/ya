-- GUI framework: layout + widgets + compose + tooling + host.
-- Rendering (ya-render-resources / ya-render-2d) lives in Framework/Render
--   ya-gui-compose    viewport/UI compose pass (consumes UIFrameSnapshot)

includes("./Runtime/Widgets/xmake.lua")
includes("./Runtime/Compose/xmake.lua")
includes("./Tooling/xmake.lua")
includes("./Host/xmake.lua")

-- GUI framework aggregate: the single link target for pure-GUI code. It
-- carries no sources of its own; public deps re-export the GUI *library*
-- closure (foundation + RHI backend + widgets + compose). Code that is not the
-- library -- the windowless app main chain and the Workbench demo app -- is
-- deliberately not re-exported, so "link the GUI framework" means the GUI
-- library and nothing else. The standalone native app host
-- (window/SDL/present) is a separate library, ya-gui-host; executable
-- consumers link that host directly.
target("ya-gui-framework")
    set_kind(ya_meta_kind())
    -- Single empty TU so the shared facade has a DLL entry point (the target
    -- itself carries no real sources; see Module.cpp).
    add_files("Module.cpp", { unity_ignored = true })
    -- Only the GUI library. Each dep that used to sit here was doing something
    -- the aggregate should not do on a consumer's behalf:
    --
    --   ya-app-kernel / ya-app-control -- the windowless main chain. ya-gui-host
    --     names the kernel because a host *is* the app. A consumer of the GUI
    --     library is not, and re-exporting it made "GUI framework" read as
    --     "GUI + the app shell". Anything that really drives the kernel (the
    --     host, AppKernelTest) names it.
    --   ya-hierarchy -- not one file under GUI/ includes it.
    --   ya-gui-tooling -- the Workbench demo app, not part of the library.
    --     GUIWorkbench names it.
    add_deps(
        "ya-foundation-core",
        "ya-rhi",
        "ya-rhi-backend-common",
        "ya-rhi-vulkan",
        "ya-render-resources",
        "ya-render-2d",
        "ya-gui-widgets",
        "ya-gui-compose",
        { public = true })
