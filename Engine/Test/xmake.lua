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
