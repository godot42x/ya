includes("./Plugins/Plugins.xmake.lua")

--add_requires("vulkansdk")
add_requires("spdlog")
add_requires("libsdl3")
add_requires("libsdl3_image")
add_requires("glm")
add_requires("spirv-cross")
add_requires("assimp")

-- just for temp debug in runtime
add_requires("imgui", {
    configs = {
        sdl3 = true,
        sdl3_gpu = true,
        debug = is_mode("debug")
    }
})


add_requires("shaderc")

target("Neon")
do
    set_kind("binary")
    add_files("./Source/**.cpp")
    add_headerfiles("./Source/**.h")

    add_includedirs("./Source")

    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflect.cc")
    -- add_deps("log")

    -- add_packages("vulkansdk")
    add_packages("spdlog")
    add_packages("libsdl3")
    add_packages("libsdl3_image")
    add_packages("glm")
    add_packages("shaderc")
    add_packages("spirv-cross")
    add_packages("imgui")
    --add_packages("glad")
    add_packages("assimp")

    -- set_runtimes("MT")

    -- Add subsystem specification to fix LNK4031 warning
    if is_plat("windows") then
        add_ldflags("/subsystem:console")
    end

    before_run(function(target)
        print("before run", target:name())
        print("removing sdl log files")
        os.rm("$(projectdir)/Neon.*-*-*.log")
    end)
end
