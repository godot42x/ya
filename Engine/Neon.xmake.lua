includes("./Plugins/Plugins.xmake.lua")

--add_requires("sdl")

--add_requires("vulkansdk")
add_requires("spdlog")
add_requires("libsdl3")
add_requires("glm")
add_requires("spirv-cross")

--add_requires("glad")
--if is_plat("windows") then
--	add_requires("opengl")
--end
add_requires("shaderc")

target("Neon")
do
    set_kind("binary")
    add_files("Source/**.cpp")
    add_headerfiles("Source/**.h")

    add_deps("utility.cc")
    add_deps("log.cc")
    -- add_deps("log")

    --add_packages("glfw")
    --add_packages("vulkansdk")
    add_packages("spdlog")
    add_packages("libsdl3")
    add_packages("glm")
    add_packages("shaderc")
    add_packages("spirv-cross")
    --add_packages("glad")

    add_includedirs("./Source")
end
