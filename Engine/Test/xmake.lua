add_requires("gtest")

-- Engine test runner + module fixture depend on the full engine aggregate,
-- so they are engine-profile only; the GUI closure test below is the single
-- test target that also exists in the gui profile.
if get_config("ya_profile") ~= "gui" then
    target("ya-module-fixture")
    do
        set_kind("shared")
        add_files("./Fixture/ModuleFixture.cpp")
        add_deps("ya-engine")
    end

    target("ya-testing")
    do
        set_kind("binary")
        add_files("./Source/**.cpp")

        add_deps("ya-engine", "ya-module-fixture")
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
        add_files("./Source/TestEntry.cpp", "./Source/ECSTest.cpp")
        add_deps("ya-ecs-core")
        add_packages("gtest")
    end

    target("ya-resource-core-test")
    do
        set_kind("binary")
        add_files("./Source/TestEntry.cpp",
                  "./Source/PathRegistryTest.cpp",
                  "./Source/ResourceTableTest.cpp")
        add_deps("ya-resource-core", "ya-foundation-core")
        add_packages("gtest")
    end

    target("ya-render-3d-test")
    do
        set_kind("binary")
        add_files("./Source/TestEntry.cpp",
                  "./Source/DeferredRenderPipelineTest.cpp",
                  "./Source/DirectionalShadowMathTest.cpp",
                  "./Source/RenderGraphCoreTest.cpp")
        add_deps("ya-render-3d", "ya-render-graph", "ya-foundation-core")
        add_packages("gtest")
    end

    -- Resource-runtime closure: links ONLY the resource line
    -- (foundation + RHI + backend + resource core/loader/runtime). Fails to
    -- link if resource code reaches ECS/Scene/Render3D/Host again.
    target("ya-resource-runtime-closure-test")
    do
        set_kind("binary")
        add_files("./Source/TestEntry.cpp",
                  "./Source/ResourceRuntimeClosureTest.cpp")
        add_deps("ya-resource-runtime")
        add_packages("gtest")
    end

    -- Vulkan backend build/link closure: proves ya-rhi-vulkan is
    -- independently consumable without GUI/Render3D/Host.
    target("ya-rhi-vulkan-smoke")
    do
        set_kind("binary")
        add_files("./Source/TestEntry.cpp",
                  "./Source/RHIVulkanSmoke.cpp")
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
    add_files("./Source/Node2DLayoutTest.cpp")
    add_files("./Source/UISceneRendererTest.cpp")
    add_files("./Source/Render2DClipTest.cpp")
    add_files("./Source/WidgetTreeTest.cpp")
    add_files("./Source/TestEntry.cpp")

    add_deps("ya-gui-framework")
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
    add_files("./Source/WidgetTreeTest.cpp")
    add_files("./Source/TestEntry.cpp")

    add_deps("ya-gui-widgets")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end
