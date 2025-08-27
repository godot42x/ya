includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/xmake.lua")
includes("./Test/xmake.lua")

add_requires(
    "spdlog",
    "libsdl3",
    "libsdl3_image",
    "glm",
    "spirv-cross",
    "stb",
    "cxxopts"
)
add_requires("assimp", {
    configs = {
        shared = false,
        runtimes = "MT",
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
        runtimes = "MT",
    }
})
add_requires("vulkansdk", {
    configs = {
        utils = {
            "VkLayer_khronos_validation", -- import layer

        }
    }
})
add_requires("glad")
add_requires("entt v3.15.0")

rule("testing")
do
    on_config(function(t)
        t:remove("files", "**.test.cpp")
    end)
end

target("ya") --"Yet Another (Game) Engine"
do
    set_kind("binary")
    add_files("./Source/**.cpp")
    add_headerfiles("./Source/**.h")

    add_rules("testing")
    add_includedirs("./Source", { public = true })

    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflect.cc")
    -- add_deps("log")


    -- Add math library for exp2 and log2 functions
    if is_plat("windows") then
        add_ldflags("/NODEFAULTLIB:LIBCMT") -- Fix runtime library conflict
    end


    add_packages("stb")
    -- add_packages("spdlog")
    add_packages("libsdl3", { public = true })
    add_packages("libsdl3_image")
    add_packages("glm", { public = true })
    add_packages("imgui", { public = true })
    --add_packages("glad")
    add_packages("assimp")

    do
        --NOTICE: must before vulkansdk or it will cause error
        -- because vulkansdk's linkdir contains those libs like ["shaderc.lib", "spirv-cross.lib"]
        add_packages("shaderc")
        add_packages("spirv-cross", { public = true })

        add_packages("vulkansdk")
    end
    add_packages("glad")
    add_packages("cxxopts", { public = true })
    add_packages("entt")

    -- add_deps("shader")

    -- Add subsystem specification to fix LNK4031 warning
    if is_plat("windows") then
        add_ldflags("/subsystem:console")
        add_defines("NOMINMAX") -- Disable min and max macros
    end

    before_run(function(target)
        print("before run", target:name())
        print("removing sdl log files")
        os.rm("$(projectdir)/ya.*-*-*.log")
    end)
end

target("ya-testing")
do
end
