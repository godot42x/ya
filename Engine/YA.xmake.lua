includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/Shader.xmake.lua")
includes("./Test/xmake.lua")
includes("./Programs/Programs.xmake.lua")
includes("./ThirdParty/ThirdParty.xmake.lua")
-- includes("./Source/Editor/xmake.lua")

add_requires(
    "spdlog"
    , "libsdl3_image"
    , "glm"
    , "stb"
    , "cxxopts"
    , "lua v5.4.8"
    , "freetype"
    , "nlohmann_json v3.12.0"
    , "sol2"
    , "glad"
    , "ktx"
)
add_requireconfs("freetype", {
    system = false,
})


add_requires("libsdl3", {
    configs = {
        debug = is_mode("debug"),
    }
})
add_requires("assimp", {
    configs = {
        shared = false,
        runtimes = "MD",
        cxxflags = "-std=c++20",
    }
})





add_requires("vulkansdk", {
    configs = {
        utils = {
            -- "VkLayer_khronos_validation", -- import layer
            "slang",
            "shaderc",
            "shaderc_util",
            "shaderc_combined",
            "shaderc_shared",
            "spirv-cross-core",
            "spirv-cross-util",
            "spirv-cross-reflect",
            "spirv-cross-glsl",


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
        print("-- ENABLE UNITY BUILD")
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Core/**.cpp", { unity_group = "Core" })
        add_files("./Source/Bus/**.cpp", { unity_group = "Bus" })
        add_files("./Source/Platform/**.cpp", { unity_group = "Platform" })
        add_files("./Source/Resource/**.cpp", { unity_group = "Resource" })
        add_files("./Source/Render/**.cpp", { unity_group = "Renderer" })
        add_files("./Source/ECS/**.cpp", { unity_group = "ECS" })
        add_files("./Source/Scene/**.cpp", { unity_group = "Scene" })
        add_files("./Source/Editor/**.cpp", { unity_group = "Editor" })
    end
    -- Root source files (ImGuiHelper.cpp, WindowProvider.cpp)
    add_files("./Source/**.cpp")
    remove_files("./Source/Platform/Render/OpenGL/**.cpp") -- develop vulkan mainly for now

    add_headerfiles("./Source/**.h")
    set_pcheader("./Source//FWD.h")

    add_includedirs("./Source", { public = true })

    -- Include generated Slang headers (auto-generated from .slang files)
    add_includedirs("./Shader/Slang/Generated", { public = true })
    add_includedirs("./Shader/GLSL/Generated", { public = true })

    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflect.cc")
    add_deps("reflects-core")
    add_deps("imgui-local")
    add_deps("imguizmo-local")


    if is_plat("windows") then
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
    --add_packages("glad")
    add_packages("assimp")
    add_packages("ktx")

   
    add_packages("vulkansdk", { public = true })
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
        add_cxxflags( "/Zm1000") -- the memory allocation for compiler increased to 1000MB
        add_ldflags("/ignore:4099") -- warning LNK4099, eg: PDB 'ya.pdb' was not found with 'ya.exe'

        if bEnableUnity then
            add_cxxflags("/bigobj") -- allow to generate big obj for big module
        else
            add_cxxflags("/bigobj") -- App.cpp also so big now...
        end
        if is_mode("debug") then
            add_ldflags("/ignore:4324") -- eg:  warning C4324: 'ya::PhongMaterialSystem::PointLightData': structure was padded due to alignment specifier
            add_cxxflags("/O0")         -- disable optimization
        end
    end

    before_build(function (target)
        print(target:name())
        os.exec("xmake ya-shader")
        
    end)

    before_run(function(target)
        print("before run", target:name())
        print("removing sdl log files")
        os.rm("$(projectdir)/ya.*-*-*.log")
    end)


end
