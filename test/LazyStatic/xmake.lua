target("lazy-static-dll1")
do
    set_kind("shared")
    set_languages("c++17")
    add_files("dll1.cpp")
    add_defines("LAZYSTATIC_EXPORTS")
end

target("lazy-static-dll2")
do
    set_kind("shared")
    set_languages("c++17")

    add_files("dll2.cpp")
    add_deps("lazy-static-dll1")
end


target("lazy-static-app1")
do
    set_kind("binary")
    set_languages("c++17")

    add_includedirs("include")
    add_files("app1.cpp")
    add_deps("lazy-static-dll1")
    add_deps("lazy-static-dll2")
end
