add_requires("lua")

includes("./yalua_test/")

target("yalua")
do
    set_kind("shared")
    add_defines("TYPE_BUILD_SHARED")

    add_packages("lua", { public = true })

    add_files("./src/yalua/**.cpp")
    add_headerfiles("./src/yalua/**.h", { public = true })
    add_includedirs("./src", { public = true })




    -- NOTICE: currently this dep is private
    add_deps("utility.cc")
end
