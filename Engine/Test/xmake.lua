add_requires("gtest")

target("ya-testing")
do
    set_kind("binary")
    add_files("./Source/**.cpp")

    add_deps("ya")
    add_packages("gtest")

    if is_plat("windows") then
        -- /utf-8
        add_cxxflags("/utf-8")
    end
end
