add_requires("gtest")

target("ya-testing")
do
    set_kind("binary")
    add_files("./Source/**.cpp")

    add_deps("ya")
    add_packages("gtest")
end
