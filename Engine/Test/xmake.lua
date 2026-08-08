add_requires("gtest")

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
    add_files("./Source/TestEntry.cpp")

    add_deps("ya-gui-framework")
    add_packages("gtest")

    if is_plat("windows") then
        add_cxxflags("/utf-8")
    end
end
