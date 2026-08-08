includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/Shader.xmake.lua")
includes("./Test/xmake.lua")
includes("./Programs/Programs.xmake.lua")
includes("./ThirdParty/ThirdParty.xmake.lua")
includes("./Source/xmake.lua")

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
    configs = {
        -- FontManager (GUI) and the imgui freetype extension both use
        -- freetype; a shared image avoids duplicating the library.
        shared = true,
    },
})
-- TEMP(unblock install): pin to cached versions while github downloads stall
add_requireconfs("cmake", {
    version = "4.2.3",
})


add_requires("libsdl3", {
    configs = {
        debug = is_mode("debug"),
        -- Single shared SDL image: several module dylibs (core window/input,
        -- imgui backend) consume SDL. A static copy per dylib duplicates the
        -- whole library (incl. ObjC classes) and splits global state.
        shared = true,
    }
})
add_requires("assimp v6.0.4", {
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
        { pattern = "#include%s*[<\"]%s*Editor/", label = "Editor" },
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
                        raise("ya-runtime isolation violation: %s includes %s outside Host/GUI", sourceFile, forbidden.label)
                    end
                end
            end
        end
    end
end



-- ==========================================================================
-- Module libraries (engine modularization).
-- Per-module targets live next to their sources, organized in three product
-- tiers: Foundation/ (core + RHI + backend), Framework/ (GUI, game product
-- line), Product/ (host + editor). Modules are shared libraries themselves;
-- `ya-engine` is the transition-period aggregate that re-exports every module
-- as public deps so editor / examples / tests keep linking one entry point.
-- ==========================================================================

target("ya-engine")
do
    set_kind("shared")
    add_defines("YA_SHARED=1")
    -- Every module export macro, so the precompiled header can parse any
    -- engine header without depending on a central macro table (see Api.h).
    ya_engine_defines()
    -- Consumers (editor / examples / tests) link the import side.
    add_defines("YA_SHARED=1", { public = true })
    before_build(function(target)
        check_runtime_source_isolation()
    end)

    -- The aggregate carries no engine TU of its own; every engine source
    -- lives in a module target (single-header third-party implementations are
    -- owned by their consuming modules). The only file here is the ImGui demo.
    add_files("./ThirdParty/ImGui/imgui_demo.cpp", { unity_ignored = true })

    add_headerfiles("./Source/**.h")
    set_pcheader("./Source/Foundation/Core/Common/FWD.h")

    add_includedirs("./Shader/Slang/Generated", { public = true })
    add_includedirs("./Shader/GLSL/Generated", { public = true })

    -- Public deps: consumers linking ya-engine transitively link every module
    -- shared library and receive each module's public include/define config.
    add_deps(
        "ya-foundation-core",
        "ya-foundation-rhi",
        "ya-foundation-rhi-backend",
        "ya-gui-runtime",
        "ya-scene-core",
        "ya-scene-runtime",
        "ya-scene-serialization",
        "ya-scene-3d",
        "ya-ecs-core",
        "ya-gameplay-ecs",
        "ya-gameplay-systems",
        "ya-component-linkage",
        "ya-render-ecs-adapters",
        "ya-resource-core",
        "ya-resource-loader",
        "ya-resource-runtime",
        "ya-render-graph",
        "ya-render-3d",
        "ya-physics",
        "ya-host",
        { public = true })
    add_deps("utility.cc", "log.cc", "reflects-core", { public = true })
    add_deps("imgui-local")
    add_deps("imguizmo-local")

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
    add_packages("libsdl3", { public = true })
    add_packages("libsdl3_image")
    add_packages("asio")
    add_packages("glm", { public = true })
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

    -- Add subsystem specification to fix LNK4031 warning
    if is_plat("windows") then
        add_ldflags("/subsystem:console")
        add_syslinks("ws2_32")
        add_defines("NOMINMAX")     -- Disable min and max macros
        add_cxxflags("/utf-8")      -- Enable UTF-8 source code support for Unicode characters
        add_cxxflags("/Zm1000")     -- the memory allocation for compiler increased to 1000MB
        add_ldflags("/ignore:4099") -- warning LNK4099, eg: PDB 'ya.pdb' was not found with 'ya.exe'
        add_cxxflags("/bigobj")
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
