add_rules("mode.debug", "mode.release")

set_languages("c++2a")

add_requires("glew","glfw","glm","spdlog")
add_requires("gtest")

add_shflags("-fPIC",{force = true})
--set_targetdir("./bin")

if is_mode("debug") then 
    add_cxxflags("-Wall ",{force=true})
    add_cxxflags("-O0")
end



    

target("Gloria")
    set_kind("headeronly")
    add_headerfiles("./include/Gloria/**.hpp")
    add_includedirs("./include/Gloria/")
    add_packages("glfw","glew", "glm","spdlog")


target("main")
    set_kind("binary")
    add_files("src/main.cc" )
    add_files("src/precompile.cc" )
    add_includedirs("./include/Gloria/")
    add_deps("Gloria")
    add_packages("glfw","glew", "glm", "opengl","spdlog")


target("test")
    add_files("test/*.cc")
    add_includedirs("./include/Gloria/")
    add_deps("Gloria")
    add_packages("gtest")
    add_packages("glfw","glew", "glm","spdlog")
