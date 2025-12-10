add_requires("gtest")

target("reflects-core-test")
do
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("gtest")
    add_deps("reflects-core")
end
