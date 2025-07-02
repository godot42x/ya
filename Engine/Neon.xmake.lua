-- if is_mode("debug") then
--     set_runtimes("MTd")
-- else
set_runtimes("MT") -- use static link and no debug runtime library
-- end

includes("./Plugins/Plugins.xmake.lua")

add_requires("spdlog")
add_requires("libsdl3")
add_requires("libsdl3_image")
add_requires("glm")
add_requires("spirv-cross")
add_requires("assimp", {
    configs = {
        shared = false,
        runtime = "MT",
    }
})

-- just for temp debug in runtime
add_requires("imgui", {
    configs = {
        sdl3 = true,
        sdl3_gpu = true,
        vulkan = true,
        debug = is_mode("debug")
    }
})
add_requires("shaderc", {
    configs = {
        shared = false,
        runtime = "MT",
    }
})

add_requires("stb")

add_requires("vulkansdk", {
    configs = {
        utils = {
            "VkLayer_khronos_validation", -- import layer
        }
    }
})
add_requires("glad")

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


    -- Add math library for exp2 and log2 functions
    if is_plat("windows") then
        add_ldflags("/NODEFAULTLIB:LIBCMT")  -- Fix runtime library conflict
    end


    add_packages("stb")
    add_packages("spdlog")
    add_packages("libsdl3")
    add_packages("libsdl3_image")
    add_packages("glm")
    add_packages("imgui")
    --add_packages("glad")
    add_packages("assimp")

    do
        --NOTICE: must before vulkansdk or it will cause error
        -- because vulkansdk's linkdir contains those libs like ["shaderc.lib", "spirv-cross.lib"]
        add_packages("shaderc")
        add_packages("spirv-cross")

        add_packages("vulkansdk")
    end
    add_packages("glad")


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
