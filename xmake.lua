add_rules("mode.debug", "mode.release")

set_languages("c++17")

add_requires("glad","glfw", "opengl", "glm")


target("ownkit")
    set_kind("static")
    add_files("src/OwnKit/*.cc")
    add_includedirs("./include/GLX/ownkit/")
    add_packages("glad", "glm")

target("glinternal")
    set_kind("static")
    add_files("src/glinternal/*.cc")
    add_includedirs("./include/GLX/glinternal/")
    add_packages("glfw","glad", "glm")
    

target("GLX")
    set_kind("binary")
    add_files("src/main.cc")
    add_deps("ownkit", "glinternal")
    add_packages("glfw","glad", "glm", "opengl")
    add_includedirs("./include/")
