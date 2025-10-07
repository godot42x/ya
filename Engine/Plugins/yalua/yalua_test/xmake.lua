add_requires("gtest")

target("yalua_test")
do
    set_kind("binary")
    set_rundir(os.scriptdir())
    set_languages("c++20")

    add_deps("yalua", { public = true })

    add_files("./**.cpp")
    add_packages("gtest", { public = true })
end
