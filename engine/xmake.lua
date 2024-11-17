
includes("plugin")

add_requires("glfw")
add_requires("vulkansdk")
add_requires("spdlog")
add_requires("libsdl")
add_requires("glm")

add_requires("glad")
if is_plat("windows") then
    add_requires("opengl")
end

target("neon")
do
    set_kind("binary")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_packages("glfw")
    add_packages("vulkansdk")
    add_packages("spdlog")
    add_packages("libsdl")
    add_packages("glm")
    add_packages("glad")

    add_includedirs("./src")
end
