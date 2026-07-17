target("yalua")
do
    set_kind("shared")
    add_defines("TYPE_BUILD_SHARED")

    add_packages("lua", { public = true })

    add_files("./**.cpp")
    add_headerfiles("./**.h", { public = true })
    add_includedirs("./include", { public = true })


    -- NOTICE: currently this dep is private
    add_deps("utility.cc")
end