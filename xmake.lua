add_rules("mode.debug", "mode.release")

set_languages("c++23")
set_targetdir("./bin")

add_requires("glew","glfw", "opengl", "glm","spdlog")
add_shflags("-fPIC")


target("config")
    set_kind("headeronly" )
    add_headerfiles("include/GLX/config/*.h")

target("logx")
    set_kind("shared")
    add_files("src/logx/*.cc")
    add_includedirs("include/GLX/")
    add_packages("spdlog")


target("ownkit")
    set_kind("shared")
    add_files("src/OwnKit/*.cc")
    add_includedirs("./include/GLX/")
    add_packages( "glm")

target("glinternal")
    set_kind("shared")
    add_files("src/glinternal/*.cc")
    add_includedirs("./include/GLX/")
    add_packages("glfw","glew", "glm","spdlog")
    

target("GLX")
    set_kind("binary")
    add_files("src/main.cc")
    add_includedirs("./include/GLX/")
    add_deps("ownkit", "glinternal","logx")
    add_packages("glfw","glew", "glm", "opengl","spdlog")
