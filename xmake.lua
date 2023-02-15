add_rules("mode.debug", "mode.release")

set_languages("c++17")

add_requires("glew","glfw", "opengl", "glm","spdlog")

target("logx")
    set_kind("static")
    add_files("src/logx/*.cc")
    add_includedirs("include/GLX/")
    add_packages("spdlog")


target("ownkit")
    set_kind("static")
    add_files("src/OwnKit/*.cc")
    add_includedirs("./include/GLX/")
    add_packages("glew", "glm","spdlog")

target("glinternal")
    set_kind("static")
    add_files("src/glinternal/*.cc")
    add_includedirs("./include/GLX/")
    add_packages("glfw","glew", "glm","spdlog")
    

target("GLX")
    set_kind("binary")
    add_files("src/main.cc")
    add_includedirs("./include/GLX/")
    add_deps("ownkit", "glinternal","logx")
    add_packages("glfw","glew", "glm", "opengl","spdlog")
