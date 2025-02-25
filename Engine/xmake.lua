-- includes("plugin")

--add_requires("sdl")
--add_requires("vulkansdk")
add_requires("spdlog")
add_requires("libsdl3")
add_requires("glm")

--add_requires("glad")
--if is_plat("windows") then
--	add_requires("opengl")
--end

target("neon")
do
	set_kind("binary")
	add_files("Source/**.cpp")
	add_headerfiles("Source/**.h")
	--add_packages("glfw")
	--add_packages("vulkansdk")
	add_packages("spdlog")
	add_packages("libsdl3")
	add_packages("glm")
	--add_packages("glad")

	add_includedirs("./src")
end
