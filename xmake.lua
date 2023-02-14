add_rules("mode.debug", "mode.release")

set_languages("c++17")

add_requires("glew","glfw", "opengl", "glm")




target("ownkit")
    set_kind("shared","windows")
    add_files("src/OwnKit/*.cc")
    add_includedirs("./include/GLX/ownkit/")
    add_packages("glew", "glm")

target("glinternal")
    set_kind("shared","windows")
    add_files("src/glinternal/*.cc")
    add_includedirs("./include/GLX/glinternal/")
    add_packages("glfw","glew", "glm")
    

target("GLX")
    set_kind("binary")
    add_files("src/main.cc")
    add_packages("glfw","glew", "glm", "opengl")
    add_deps("ownkit", "glinternal")
    add_includedirs("./include/")