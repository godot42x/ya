-- function include_project(name)
--     includes(name .. "/src")
-- end

set_targetdir("./bin")


add_rules("mode.debug", "mode.release")

set_languages("c++20")

add_requires("glfw")
add_requires("vulkansdk")
add_requires("spdlog")
-- add_requires("glew", "glfw", "glm", "spdlog")
-- add_requires("gtest")
-- add_requires("imgui docking", { configs = { glfw = true, opengl3 = true } })

target("neon")
do
    set_kind("binary")
    add_files("main.cpp")
    add_packages("glfw")
    add_packages("vulkansdk")
    add_packages("spdlog")
end
