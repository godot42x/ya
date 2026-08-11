add_requires("gtest")

-- GUIWorkbench: the product-level probe for the GUI framework (Phase 1 shell,
-- Phase 3 = real tool workspace). A standalone binary consuming the same
-- ya-gui-app-host library as GUIFrameworkSmoke; it never copies the
-- SDL/Vulkan frame loop and never depends on Scene/ECS/Render3D/Host/Editor.
target("GUIWorkbench")
    set_kind("binary")
    add_files("./Source/**.cpp")
    add_deps("ya-gui-app-host")

-- ToolWorkspace unit tests: the workspace is pure document/selection/command
-- state, so this target links only the workspace TU + gtest, no GUI closure.
target("ya-gui-workbench-workspace-test")
    set_kind("binary")
    add_includedirs("./Source")
    add_files("./Source/GUIWorkbenchWorkspace.cpp")
    add_files("./Test/TestEntry.cpp", "./Test/WorkspaceTest.cpp")
    add_deps("ya-gui-widgets", "ya-foundation-core")
    add_packages("gtest")
