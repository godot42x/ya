add_rules("mode.debug", "mode.releasedbg", "mode.release", "mode.profile")
set_languages("c++20")

-- NOTE (macOS Vulkan SDK): installed by Script/setup_vulkan_sdk_macos.py into
-- the project-local shared cache (<main project>/.ya-cache/VulkanSDK;
-- $YA_CACHE_ROOT / git config ya.cacheRoot / $YA_VULKAN_SDK_ROOT override);
-- each checkout exposes it as a symlink at
-- Engine/ThirdParty/VulkanSDK/<version>/macOS, auto-discovered by
-- Xmake/package/vulkan/xmake.lua so multiple worktrees / parallel agents
-- share one SDK copy. Deleting the project removes the cache too.
-- No setup-env.sh sync step is required.

if is_plat("windows") then
    set_exceptions("cxx")
    set_runtimes("MD") -- use dynamic CRT to match VulkanSDK prebuilt libs (shaderc_combined etc.)
end


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
includes("./Test/xmake.lua")

-- add_rules("SourceFiles")

set_rundir(os.scriptdir())


add_rules("plugin.compile_commands.autoupdate", { outputdir = os.scriptdir() })


includes("./Example/xmake.lua")
