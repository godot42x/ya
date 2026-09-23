add_requires("gtest")

-- `xmake test` (and `make test`): build and run the whole `test` group, so the
-- sweep lives next to the targets it sweeps instead of next to the scratch
-- prototypes. Groups resolve through set_group("test") on every test target;
-- target order is xmake's, and the sweep stops at the first red target, so a
-- failure means "fix and re-run" rather than "here is the full report".
task("test")
do
    set_menu {
        usage = "xmake test",
        options = {
            { nil, "rule", "v", "debug", "the rule to config build mode " }
        }
    }
    on_run(function()
        os.exec("xmake b -g test")
        os.exec("xmake r -g test")
    end)
end

-- Test sources are collected by DIRECTORY, one directory per gate, so a new
-- test file joins its gate the moment it lands in the right folder -- no edit
-- here, no per-file bookkeeping. Layout contract:
--
--   Source/Support/     shared runner (gtest main) -- every target batches it
--   Source/Integration/ full-engine tests -- reached through the `**.cpp` glob
--                       below, so they only ever run in `ya-testing`
--   Source/<Gate>/      minimal-closure regression suites; the gate name is the
--                       linkage promise the target enforces (see add_deps)
--
-- A directory glob selects whole suites, never individual cases. Picking a
-- single file out of a suite directory again means the file sits in the wrong
-- folder: move it instead of naming it.
function ya_test_sources(...)
    -- Support/ headers are addressed by name from every suite directory.
    add_includedirs("./Source/Support")
    for _, dir in ipairs({ ... }) do
        add_files("./Source/" .. dir .. "/*.cpp")
    end
end

-- Engine test runner + module fixture depend on the full engine aggregate,
-- so they are engine-profile only; the GUI closure test below is the single
-- test target that also exists in the gui profile.
if get_config("ya_profile") ~= "gui" then
    target("ya-module-fixture")
    do
        set_kind("shared")
        add_files("./Fixture/*.cpp")
        add_deps("ya-engine")
    end

    target("ya-testing")
    do
        set_kind("binary")
        set_group("test")
        add_files("./Source/**.cpp")
        add_includedirs("./Source/Support")

        add_deps("ya-engine", "ya-module-fixture", "ya-game-editor")
        -- The engine tests drive the App shell directly.
        add_deps("ya-game-runtime")
        add_packages("gtest")
        add_packages("quickjs-ng")
        add_packages("asio")

        if is_plat("windows") then
            -- /utf-8
            add_cxxflags("/utf-8")
        end
    end

    -- Minimal closure targets: fast per-module regression gates.
    target("ya-ecs-core-test")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "EcCore")
        add_deps("ya-ecs-core")
        add_packages("gtest")
    end

    target("ya-resource-core-test")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "ResourceCore")
        add_deps("ya-resource-core", "ya-foundation-core")
        add_packages("gtest")
    end

    target("ya-render-3d-test")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "Render3D")
        -- Plan tests declare real Scenes: a Scene handle, not an invented id, is
        -- what a view declaration carries.
        add_deps("ya-render-3d", "ya-render-graph", "ya-foundation-core", "ya-scene-core")
        add_packages("gtest")
    end

    -- Render2D closure test: the 2D batching/clip layer moved out of the GUI
    -- framework (see commit 9c24c071), so its regression guards live in the
    -- render line, not the GUI closure.
    target("ya-render-2d-test")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "Render2D")
        add_deps("ya-render-2d")
        add_packages("gtest")
    end

    -- Resource-runtime closure: links ONLY the resource line
    -- (foundation + RHI + backend + resource core/loader/runtime). Fails to
    -- link if resource code reaches ECS/Scene/Render3D/Host again.
    target("ya-resource-runtime-closure-test")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "ResourceRuntime")
        add_deps("ya-resource-runtime")
        add_packages("gtest")
    end

    -- Vulkan backend build/link closure: proves ya-rhi-vulkan is
    -- independently consumable without GUI/Render3D/Host.
    target("ya-rhi-vulkan-smoke")
    do
        set_kind("binary")
        set_group("test")
        ya_test_sources("Support", "RhiVulkan")
        add_deps("ya-rhi-vulkan")
        add_packages("gtest")
    end
end

-- GUI closure test: links ONLY the GUI framework closure
-- (foundation + RHI + backend + gui-runtime). It is the regression guard for
-- the "pure GUI host" product line: if GUI code ever reaches into
-- resource / ecs / render-3d / physics / host / editor again, this target
-- fails to link.
target("ya-gui-closure-test")
do
    set_kind("binary")
    set_group("test")
    ya_test_sources("Support", "GuiDeclarative", "GuiWidgets", "GuiFramework")

    add_deps("ya-gui-framework")
    -- This target also covers AppKernelTest, which drives the windowless main
    -- chain directly. The kernel is not part of the GUI library, so it is
    -- named here rather than inherited from the aggregate.
    add_deps("ya-app-kernel")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end

-- WidgetTree closure test: links ONLY ya-gui-widgets (foundation + the GUI
-- draw2d/resources deps come in transitively). Proves the Game UI visual
-- tree has no Scene/ECS/Render3D/Host dependency and no direct RHI headers.
target("ya-gui-widgets-test")
do
    set_kind("binary")
    set_group("test")
    ya_test_sources("Support", "GuiDeclarative", "GuiWidgets")

    add_deps("ya-gui-widgets", "ya-render-resources")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end

-- Phase -1A contract gate: keeps the declarative UI prerequisites isolated
-- from legacy widget tests while the retained framework is being migrated.
target("ya-gui-declarative-contract-test")
do
    set_kind("binary")
    set_group("test")
    ya_test_sources("Support", "GuiDeclarative")
    add_deps("ya-gui-widgets", "ya-render-resources")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end

-- Headless GUI host regression: exercises AppKernel -> delegate -> WidgetTree
-- -> immutable snapshot without creating SDL/Vulkan presentation resources.
target("ya-gui-headless-host-test")
do
    set_kind("binary")
    set_group("test")
    ya_test_sources("Support", "GuiHost")

    add_deps("ya-gui-host")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end
