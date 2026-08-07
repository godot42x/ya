includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/Shader.xmake.lua")
includes("./Test/xmake.lua")
includes("./Programs/Programs.xmake.lua")
includes("./ThirdParty/ThirdParty.xmake.lua")
-- includes("./Source/Editor/xmake.lua")

add_requires(
    "spdlog"
    , "libsdl3_image"
    , "asio"
    , "glm"
    , "stb"
    , "tinygltf v2.9.6"
    , "cxxopts"
    , "lua v5.4.8"
    , "freetype"
    , "nlohmann_json v3.12.0"
    , "sol2"
    , "glad"
    , "ktx"
    ,"joltphysics v5.5.0"
    ,"quickjs-ng v0.15.1"
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
add_requires("vulkan-memory-allocator v3.3.0")
add_requires("entt v3.15.0", {
    -- configs = {
    -- debug = is_mode("debug"),
    -- }
})

option("ya_enable_unity-build")
do
    set_default(true)
end

local function check_runtime_source_isolation()
    local forbiddenIncludes = {
        { pattern = "#include%s*[<\"]%s*editor/", label = "Editor" },
    }
    local sourceRoot = path.join(os.scriptdir(), "Source")
    local sourceFiles = os.files(path.join(sourceRoot, "**.h"))
    table.join2(sourceFiles, os.files(path.join(sourceRoot, "**.cpp")))

    for _, sourceFile in ipairs(sourceFiles) do
        local bAllowGuiRuntime = sourceFile:find("/Runtime/GUI/", 1, true) ~= nil
        if not sourceFile:find("/Editor/", 1, true) then
            local contents = io.readfile(sourceFile):lower()
            for _, forbidden in ipairs(forbiddenIncludes) do
                if contents:find(forbidden.pattern) then
                    raise("ya-runtime isolation violation: %s includes %s", sourceFile, forbidden.label)
                end
            end
            if not bAllowGuiRuntime then
                local guiForbiddenIncludes = {
                    { pattern = "#include%s*[<\"]%s*imgui%.h", label = "ImGui" },
                    { pattern = "#include%s*[<\"]%s*imguihelper%.h", label = "ImGuiHelper" },
                    { pattern = "#include%s*[<\"]%s*imguizmo", label = "ImGuizmo" },
                }
                for _, forbidden in ipairs(guiForbiddenIncludes) do
                    if contents:find(forbidden.pattern) then
                        raise("ya-runtime isolation violation: %s includes %s outside Runtime/GUI", sourceFile, forbidden.label)
                    end
                end
            end
        end
    end
end



-- ==========================================================================
-- Module libraries (engine modularization, Phase 2: GUI closure split).
-- `ya-engine` remains the single shared aggregate export boundary; the
-- libraries below exist for incremental builds and link-time selection.
-- ==========================================================================

local bEnableUnity = get_config("ya_enable_unity-build")

target("ya-core")
do
    set_kind("static")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Core/**.cpp|Common/AssetRef.cpp|Scripting/ScriptApiCore.cpp|Scripting/ScriptApiAsset.cpp|Input/InputRouter.cpp|Profiling/Profiling.cpp|Reflection/ECSRegistry.cpp|Serialization/SceneSerializer.cpp", { unity_group = "Core" })
    end
    add_files("./Source/Core/**.cpp|Common/AssetRef.cpp|Scripting/ScriptApiCore.cpp|Scripting/ScriptApiAsset.cpp|Input/InputRouter.cpp|Profiling/Profiling.cpp|Reflection/ECSRegistry.cpp|Serialization/SceneSerializer.cpp")
    add_headerfiles("./Source/Core/**.h")
    add_includedirs("./Source", { public = true })
    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflects-core", { public = true })
    add_packages("glm", { public = true })
    add_packages("nlohmann_json", { public = true })
    add_packages("entt", { public = true })
    add_packages("libsdl3", { public = true })
end

target("ya-rhi")
do
    set_kind("static")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    add_rules("ya.shader.codegen")
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Render/RenderDefines.cpp", { unity_group = "RHI" })
        add_files("./Source/Render/Core/FrameUploadArena.cpp", { unity_group = "RHI" })
        add_files("./Source/Render/Core/RenderImage.cpp", { unity_group = "RHI" })
        add_files("./Source/Render/Core/TextureUploadService.cpp", { unity_group = "RHI" })
        add_files("./Source/Render/Core/Graph/**.cpp", { unity_group = "RHI" })
    end
    add_files("./Source/Render/RenderDefines.cpp")
    add_files("./Source/Render/Core/FrameUploadArena.cpp")
    add_files("./Source/Render/Core/RenderImage.cpp")
    add_files("./Source/Render/Core/TextureUploadService.cpp")
    add_files("./Source/Render/Core/Graph/**.cpp")
    add_headerfiles("./Source/Render/Render.h")
    add_headerfiles("./Source/Render/RenderDefines.h")
    add_headerfiles("./Source/Render/Core/**.h")
    add_headerfiles("./Source/Render/Stage/**.h")
    add_includedirs("./Source", { public = true })
    add_includedirs("./Shader/Slang/Generated", { public = true })
    add_includedirs("./Shader/GLSL/Generated", { public = true })
    add_deps("ya-core", { public = true })
    add_packages("glm", { public = true })
    add_packages("entt", { public = true })
    add_packages("stb", { public = true })
    add_packages("ktx", { public = true })
end

target("ya-rhi-backend")
do
    set_kind("static")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Render/Render.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/DescriptorSet.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/FrameBuffer.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/Pipeline.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/RenderPass.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/Swapchain.cpp", { unity_group = "Backend" })
        add_files("./Source/Render/Core/Texture.cpp", { unity_group = "Backend" })
        add_files("./Source/Platform/Render/Vulkan/**.cpp", { unity_group = "Backend" })
    end
    add_files("./Source/Render/Render.cpp")
    add_files("./Source/Render/Core/DescriptorSet.cpp")
    add_files("./Source/Render/Core/FrameBuffer.cpp")
    add_files("./Source/Render/Core/Pipeline.cpp")
    add_files("./Source/Render/Core/RenderPass.cpp")
    add_files("./Source/Render/Core/Swapchain.cpp")
    add_files("./Source/Render/Core/Texture.cpp")
    add_files("./Source/Platform/Render/Vulkan/**.cpp")
    add_headerfiles("./Source/Platform/Render/Vulkan/**.h")
    add_includedirs("./Source", { public = true })
    add_deps("ya-rhi", { public = true })
    add_packages("vulkansdk", { public = true })
    add_packages("vulkan-memory-allocator", { public = true })
    add_packages("glad", { public = true })
    add_packages("cxxopts", { public = true })
end

target("ya-ui")
do
    set_kind("static")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/UI/2D/**.cpp", { unity_group = "UI" })
        add_files("./Source/UI/Resource/**.cpp", { unity_group = "UI" })
    end
    add_files("./Source/UI/2D/**.cpp")
    add_files("./Source/UI/Resource/**.cpp")
    add_headerfiles("./Source/UI/2D/**.h")
    add_headerfiles("./Source/UI/Resource/**.h")
    add_headerfiles("./Source/UI/UIBase.h")
    add_includedirs("./Source", { public = true })
    add_includedirs("./Shader/Slang/Generated", { public = true })
    add_includedirs("./Shader/GLSL/Generated", { public = true })
    add_deps("ya-rhi", { public = true })
    add_packages("freetype", { public = true })
    add_packages("glm", { public = true })
end

target("ya-ui-scene")
do
    set_kind("static")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    if bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/UI/Scene/Node2D.cpp", { unity_group = "UIScene" })
        add_files("./Source/UI/UISceneRenderer.cpp", { unity_group = "UIScene" })
        add_files("./Source/UI/RenderViewportOverlayRecorder.cpp", { unity_group = "UIScene" })
    end
    add_files("./Source/UI/Scene/Node2D.cpp")
    add_files("./Source/UI/UISceneRenderer.cpp")
    add_files("./Source/UI/RenderViewportOverlayRecorder.cpp")
    add_headerfiles("./Source/UI/Scene/**.h")
    add_headerfiles("./Source/UI/UISceneRenderer.h")
    add_headerfiles("./Source/UI/RenderViewportOverlayRecorder.h")
    add_includedirs("./Source", { public = true })
    add_deps("ya-ui", { public = true })
end

target("ya-engine")
do
    set_kind("shared")
    add_defines("BUILD_LIBRARY=1")
    add_defines("BUILD_SHARED_YA=1", { public = true })
    before_build(function(target)
        check_runtime_source_isolation()
    end)
    add_rules("ya.shader.codegen")
    if bEnableUnity then
        print("-- ENABLE UNITY BUILD")
        add_rules("c++.unity_build", { batchsize = 2 })
        add_files("./Source/Bus/**.cpp", { unity_group = "Bus" })
        add_files("./Source/Platform/**.cpp|Platform/Render/Vulkan/**.cpp", { unity_group = "Platform" })
        add_files("./Source/Resource/**.cpp", { unity_group = "Resource" })
        add_files("./Source/Render/**.cpp|Render/Core/**.cpp|Render/Stage/**.cpp|Render/RHI/**.cpp|Render/Render.cpp|Render/RenderDefines.cpp", { unity_group = "Renderer" })
        add_files("./Source/ECS/**.cpp", { unity_group = "ECS" })
        add_files("./Source/Scene/**.cpp", { unity_group = "Scene" })
        add_files("./Source/Physics/**.cpp", { unity_group = "Physics" })
        -- add_files("./Source/Runtime/**.cpp", { unity_group = "Runtime" })
        add_files("./Source/Runtime/Rendering/**.cpp", { unity_group = "Runtime.Rendering" })
        add_files("./Source/Runtime/GUI/**.cpp", { unity_group = "Runtime.GUI" })
        add_files("./Source/Runtime/Application/**.cpp", { unity_group = "Runtime.Application" })
        add_files("./Source/Core/Common/AssetRef.cpp", { unity_group = "Resource" })
        add_files("./Source/Core/Scripting/ScriptApiCore.cpp", { unity_group = "Runtime.Application" })
        add_files("./Source/Core/Scripting/ScriptApiAsset.cpp", { unity_group = "Runtime.Application" })
        add_files("./Source/Core/Input/InputRouter.cpp", { unity_group = "Runtime.Application" })
        add_files("./Source/Core/Profiling/Profiling.cpp", { unity_group = "Runtime.Application" })
        add_files("./Source/Core/Reflection/ECSRegistry.cpp", { unity_group = "ECS" })
        add_files("./Source/Core/Serialization/SceneSerializer.cpp", { unity_group = "Scene" })
    end
    -- Root source files (ImGuiHelper.cpp, WindowProvider.cpp)
    -- Exclude Implementaion/*.cpp from the broad glob; they're added
    -- separately below with unity_ignored to keep their single-header
    -- _IMPLEMENTATION defines intact (a sibling TU in the same unity batch
    -- can include the underlying header first, which then blocks the later
    -- implementation expansion via include guard and causes unresolved
    -- symbols at link time).
    add_files("./Source/**.cpp|Implementaion/*.cpp|Core/**.cpp|UI/**.cpp|Render/Core/**.cpp|Render/Stage/**.cpp|Render/RHI/**.cpp|Render/Render.cpp|Render/RenderDefines.cpp|Platform/Render/Vulkan/**.cpp")
    remove_files("./Source/Editor/**.cpp")
    remove_files("./Source/Platform/Render/OpenGL/**.cpp") -- develop vulkan mainly for now

    add_files("./Source/Implementaion/VulkanMemoryAllocator.cpp", { unity_ignored = true })
    add_files("./Source/Implementaion/STB.cpp", { unity_ignored = true })
    add_files("./Source/Implementaion/TinyGLTF.cpp", { unity_ignored = true })
    add_files("./ThirdParty/ImGui/imgui_demo.cpp", { unity_ignored = true })
    add_files("./Source/Core/Common/AssetRef.cpp")
    add_files("./Source/Core/Scripting/ScriptApiCore.cpp")
    add_files("./Source/Core/Scripting/ScriptApiAsset.cpp")
    add_files("./Source/Core/Input/InputRouter.cpp")
    add_files("./Source/Core/Profiling/Profiling.cpp")
    add_files("./Source/Core/Reflection/ECSRegistry.cpp")
    add_files("./Source/Core/Serialization/SceneSerializer.cpp")

    add_headerfiles("./Source/**.h")
    set_pcheader("./Source//FWD.h")

    add_includedirs("./Source", { public = true })

    -- Include generated Slang headers (auto-generated from .slang files)
    add_includedirs("./Shader/Slang/Generated", { public = true })
    add_includedirs("./Shader/GLSL/Generated", { public = true })

    add_deps("utility.cc")
    add_deps("log.cc")
    add_deps("reflects-core", { public = true })
    add_deps("imgui-local")
    add_deps("imguizmo-local")
    add_deps("ya-core", "ya-rhi", "ya-rhi-backend", "ya-ui", "ya-ui-scene", { public = true })

    if is_plat("windows") then
        add_defines("IMGUI_API=__declspec(dllexport)")
        add_defines("IMGUI_IMPL_API=__declspec(dllexport)")
        add_defines("USE_IMGUI_API")
    end


    if is_plat("windows") then
        -- Debug 模式下禁用链接器优化，保留所有代码（包括静态初始化）
        if is_mode("debug") then
            add_ldflags("/OPT:NOREF", "/OPT:NOICF", { force = true })
        end
    end


    add_packages("stb")
    add_packages("tinygltf")
    -- add_packages("spdlog")
    add_packages("libsdl3", { public = true })
    add_packages("libsdl3_image")
    add_packages("asio")
    add_packages("glm", { public = true })
    --add_packages("glad")
    add_packages("assimp")
    add_packages("ktx")


    add_packages("vulkansdk", { public = true })
    add_packages("vulkan-memory-allocator", { public = true })
    add_packages("glad")
    add_packages("cxxopts", { public = true })
    add_packages("entt", { public = true })
    add_packages("lua", { public = true })
    add_packages("freetype")
    add_packages("nlohmann_json", { public = true })
    add_packages("sol2", { public = true })
    add_packages("joltphysics")
    add_packages("quickjs-ng")

    -- add_deps("shader")

    -- Add subsystem specification to fix LNK4031 warning
    if is_plat("windows") then
        add_ldflags("/subsystem:console")
        add_syslinks("ws2_32")
        add_defines("NOMINMAX")     -- Disable min and max macros
        add_cxxflags("/utf-8")      -- Enable UTF-8 source code support for Unicode characters
        add_cxxflags("/Zm1000")     -- the memory allocation for compiler increased to 1000MB
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

    before_run(function(target)
        print("before run", target:name())
        print("removing sdl log files")
        os.rm("$(projectdir)/ya.*-*-*.log")
    end)
end

target("ya-editor")
do
    set_kind("shared")
    local bEnableUnity = get_config("ya_enable_unity-build")
    if  bEnableUnity then
        add_rules("c++.unity_build", { batchsize = 3 })
    end
    add_files("./Source/Editor/**.cpp")
    add_deps("ya-engine")
    add_links("ya-engine")
    add_includedirs("./Source", { public = true })
    add_includedirs("./ThirdParty/ImGui", { public = true })
    add_includedirs("./ThirdParty/ImGui/Backends", { public = true })
    add_includedirs("./ThirdParty/ImGui/misc/cpp", { public = true })
    add_includedirs("./ThirdParty/ImGui/misc/freetype", { public = true })
    add_includedirs("./ThirdParty/ImGuizmo", { public = true })
    if is_plat("windows") then
        add_cxxflags("/bigobj")
        add_defines("IMGUI_API=__declspec(dllimport)")
        add_defines("IMGUI_IMPL_API=__declspec(dllimport)")
        add_defines("USE_IMGUI_API")
    end
end
