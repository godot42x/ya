target("test.cc-example")
do
    set_kind("static")
    set_languages("c++17")

    add_deps("test.cc")


    add_files("BasicTest.cpp")

    -- Add MSVC preprocessor flag for proper macro expansion
    if is_plat("windows") then
        add_cxxflags("/Zc:preprocessor")
    end
end
