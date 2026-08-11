add_rules("mode.debug", "mode.releasedbg", "mode.release", "mode.profile")
set_languages("c++20")

-- Product profile: which product line enters the build graph.
--   engine: full 3D engine (Game line + Host + Editor)
--   gui:    lightweight 2D framework (Core/RHI/Vulkan/GUI + minimal host)
option("ya_profile")
    set_default("engine")
    set_showmenu(true)
    set_values("engine", "gui")
    set_description("Product profile: engine (full 3D) or gui (lightweight 2D)")
option_end()

-- Module linkage: how module targets are built.
--   shared:   every module is its own shared library (DLL/dylib)
--   monolith: modules become static and are linked into each product exe
option("ya_linkage")
    set_default("shared")
    set_showmenu(true)
    set_values("shared", "monolith")
    set_description("Module linkage: shared module libraries or static monolith")
option_end()

-- NOTE (macOS Vulkan SDK): installed by Script/setup_vulkan_sdk_macos.py into
-- the MAIN project's repo-local dir Engine/ThirdParty/VulkanSDK (real files;
-- $YA_CACHE_ROOT / git config ya.cacheRoot / $YA_VULKAN_SDK_ROOT override the
-- main project); linked worktrees expose it as a symlink at
-- Engine/ThirdParty/VulkanSDK/<version>/macOS, auto-discovered by
-- Xmake/package/vulkan/xmake.lua so multiple worktrees / parallel agents
-- share one SDK copy. Deleting the main project removes everything; nothing
-- is written to system directories. No setup-env.sh sync step is required.

if is_plat("windows") then
    set_exceptions("cxx")
    set_runtimes("MD") -- use dynamic CRT to match VulkanSDK prebuilt libs (shaderc_combined etc.)
end

-- Shared xmake packages (SDL3, freetype, ...) are linked via @rpath; copy
-- them next to the produced binaries so the existing @loader_path rpath
-- resolves them without manual DYLD setup. New shared engine packages must
-- be registered here.
after_build(function(target)
    if not is_plat("macosx") then
        return
    end
    local pkgroot = path.join(os.getenv("HOME") or "", ".xmake", "packages")
    for _, name in ipairs({ "libsdl3", "freetype" }) do
        for _, libfile in ipairs(os.files(path.join(pkgroot, name:sub(1, 1), name, "**/*.dylib"))) do
            os.cp(libfile, target:targetdir())
        end
    end
end)


set_policy("build.warning", true)
set_warnings("all", "extra")
if is_plat("windows") then
    add_cxxflags(
        "/utf-8"    --  Enable UTF-8 source code support for Unicode characters
    )
    if is_mode("debug") then
        add_cxflags(
             "/wd4251"   --  needs to have dll-interface to be used by clients of class
            , "/wd4100" --  unreferenced formal parameter
            , "/wd4267" --  conversion from 'size_t' to 'type', possible loss of data
            , "/wd4819" --  character that cannot be represented in the current code page
            , "/JMC"  -- only jump to my codes when f11(not into std codes)

        )
        add_ldflags(
        -- "/ignore:4099" -- PDB not found
        )
    end

end

if is_mode("debug") then
    add_defines("YA_BUILD_MODE_DEBUG")
elseif is_mode("releasedbg") then
    add_defines("YA_BUILD_MODE_RELEASEDBG")
elseif is_mode("profile") then
    add_defines("YA_BUILD_MODE_PROFILE")
else
    add_defines("YA_BUILD_MODE_RELEASE")
end

if is_mode("debug") or is_mode("releasedbg") then
    add_defines("BUILD_DEBUG")
else
    add_defines("BUILD_NO_DEBUG")
end

if is_mode("profile") or is_mode("debug") then
    add_defines("YA_PROFILING_CONDITIONAL")
else
    add_defines("YA_PROFILING_DISABLED")
end


includes("./Xmake/Rule.lua")
includes("./Xmake/package/xmake.lua")
includes("./Engine/YA.xmake.lua")
-- Legacy standalone test targets (Test/*.cpp) depend on the full engine
-- aggregate; they are engine-profile only.
if get_config("ya_profile") ~= "gui" then
    includes("./Test/xmake.lua")
end

-- add_rules("SourceFiles")

set_rundir(os.scriptdir())


add_rules("plugin.compile_commands.autoupdate", { outputdir = os.scriptdir() })


-- Example products: the standalone GUI examples (GUIFrameworkSmoke /
-- GUIWorkbench) build in both profiles; the 3D project examples are
-- engine-profile only (guarded inside Example/xmake.lua).
includes("./Example/xmake.lua")
