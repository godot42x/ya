includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/xmake.lua")
includes("./Test/xmake.lua")
-- includes("./Source/Editor/xmake.lua")

add_requires(
    "spdlog"
    , "libsdl3_image"
    , "glm"
    , "stb"
    , "cxxopts"
    , "lua"
    , "freetype"
    , "nlohmann_json v3.12.0"
    , "sol2"
    , "glad"
    , "ktx"
)
add_requireconfs("freetype", {
    system = false,
})
add_requires("spirv-cross", {
    configs = {
        shared = true,
    }
})

add_requires("libsdl3", {
    configs = {
        debug = is_mode("debug"),
    }
})
add_requires("assimp", {
    configs = {
        shared = false,
        -- runtimes = is_mode("debug") and "MTd" or "MT",
        runtimes = "MT",
        cxxflags = "-std=c++20",
    }
})


local imgui_version_str = "imgui docking"
local imgui_configs = {
    sdl3 = true,
    sdl3_gpu = true,
    vulkan = true,
    debug = is_mode("debug"),
    docking = true,
    freetype = true, -- enable freetype support
    wchar32 = true,  -- enable wchar32 support -> emoji
}
add_requires(imgui_version_str, {
    debug = is_mode("debug"),
    system = false,
    configs = imgui_configs
})
add_requires("imguizmo-local", {
    debug = is_mode("debug"),
    configs = {
        ["imgui_version_str"] = imgui_version_str,
        ["imgui_configs"] = imgui_configs,
        docking = true,
    }
})
add_requires("shaderc", {
    configs = {
        shared = false,
        -- runtimes = "MT",
    }
})
if is_plat("windows") then
    add_requireconfs("shaderc",
        {
            configs = {
                shared = false,
                -- runtimes = "MT",
            },
        })
end

add_requires("vulkansdk", {
    configs = {
        utils = {
            "VkLayer_khronos_validation", -- import layer

        }
    }
})
add_requires("entt v3.15.0", {
    -- configs = {
    -- debug = is_mode("debug"),
    -- }
})

option("ya_enable_unity-build")
do
    set_default(true)
end



target("ya") --"Yet Another (Game) Engine"
do
    set_kind("static")
    local bEnableUnity = get_config("ya_enable_unity-build")
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Core/**.cpp", { unity_group = "Core" })
        add_files("./Source/Platform/**.cpp", { unity_group = "Platform" })
        add_files("./Source/Resource/**.cpp", { unity_group = "Resource" })
        add_files("./Source/Render/**.cpp", { unity_group = "Renderer" })
        add_files("./Source/ECS/**.cpp", { unity_group = "ECS" })
        add_files("./Source/Editor/**.cpp", { unity_group = "Editor" })
        add_files("./Source/Scene/**.cpp", { unity_group = "Scene" })
    end
    -- Root source files (ImGuiHelper.cpp, WindowProvider.cpp)
    add_files("./Source/*.cpp")
    remove_files("./Source/Platform/Render/OpenGL/**.cpp") -- develop vulkan mainly for now

    add_headerfiles("./Source/**.h")
    set_pcheader("./Source//FWD.h")

    add_includedirs("./Source", { public = true })

    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflect.cc")
    add_deps("reflects-core")


    -- Add math library for exp2 and log2 functions
    if is_plat("windows") then
        add_ldflags("/NODEFAULTLIB:LIBCMT") -- Fix runtime library conflict

        -- Debug 模式下禁用链接器优化，保留所有代码（包括静态初始化）
        if is_mode("debug") then
            add_ldflags("/OPT:NOREF", "/OPT:NOICF", { force = true })
        end
    end


    add_packages("stb")
    -- add_packages("spdlog")
    add_packages("libsdl3", { public = true })
    add_packages("libsdl3_image")
    add_packages("glm", { public = true })
    add_packages("imgui", { public = true })
    add_packages("imguizmo-local", { public = true })
    --add_packages("glad")
    add_packages("assimp")
    add_packages("ktx")

    do
        --NOTICE: must before vulkansdk or it will cause error
        -- because vulkansdk's linkdir contains those libs like ["shaderc.lib", "spirv-cross.lib"]
        add_packages("shaderc")
        add_packages("spirv-cross", { public = true })
        add_packages("vulkansdk", { public = true })
    end
    add_packages("glad")
    add_packages("cxxopts", { public = true })
    add_packages("entt", { public = true })
    add_packages("lua", { public = true })
    add_packages("freetype")
    add_packages("nlohmann_json", { public = true })
    add_packages("sol2", { public = true })

    -- add_deps("shader")

    -- Add subsystem specification to fix LNK4031 warning
    if is_plat("windows") then
        add_ldflags("/subsystem:console")
        add_defines("NOMINMAX") -- Disable min and max macros
        add_cxxflags("/utf-8")  -- Enable UTF-8 source code support for Unicode characters
        --  fatal error C1202: recursive type or function dependency context too complex
        -- add_cxxflags(
        --     "/Zm1000"   -- the memory allocation for compiler increased to 1000MB
        -- )
        add_ldflags("/ignore:4099") -- warning LNK4099, eg: PDB 'ya.pdb' was not found with 'ya.exe'

        if bEnableUnity then
            add_cxxflags("/bigobj") -- allow to generate big obj for big module
        end
        if is_mode("debug") then
            add_ldflags("/ignore:4324") -- eg:  warning C4324: 'ya::PhongMaterialSystem::PointLightData': structure was padded due to alignment specifier
            add_cxxflags("/O0")         -- disable optimization
        end
    end

    before_run(function(target)
        print("before run", target:name())
        print("removing sdl log files")
        os.rm("$(projectdir)/ya.*-*-*.log")
    end)
end
