set_targetdir("./bin")


add_rules("mode.debug", "mode.release")

set_languages("c++2a")

add_requires("glew","glfw","glm","spdlog")
add_requires("gtest")
add_requires("imgui docking",{configs={ glfw = true, opengl3=true }})




add_shflags("-fPIC",{force = true})

if is_mode("debug") then 
    add_cxxflags("-Wall ",{force=true})
    add_cxxflags("-O0")
end


    

target("Gloria")
    set_kind("shared")
    add_files("src/**.cc")
    remove_files("src/main.cc")
    add_includedirs("./include/Gloria/")
    add_packages("glfw","glew", "glm","spdlog")
	add_packages("imgui")

target("main")
    set_kind("binary")
    add_files("src/main.cc")
    add_includedirs("./include/Gloria/")
    add_packages("glfw","glew", "glm","spdlog")
	add_packages("imgui")
    add_deps("Gloria")


target("test")
    add_files("test/*.cc")
    add_includedirs("./include/Gloria/")
    add_deps("Gloria")
    add_packages("gtest")
    add_packages("glfw","glew", "glm","spdlog")

