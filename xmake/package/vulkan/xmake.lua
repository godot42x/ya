package("vulkansdk")
do
    set_homepage("https://www.lunarg.com/vulkan-sdk/")
    set_description("LunarG Vulkan® SDK")

    add_configs("bDebugOutput", {
        description = "Enable debug output.",
        default = false,
        type = "boolean",
        readonly = true
    })
    add_configs("shared", { description = "Build shared library.", default = true, type = "boolean", readonly = true })
    add_configs("utils", { description = "Enabled vulkan utilities.", default = {}, type = "table" })

    local function _repo_local_sdkdir()
        if not is_host("macosx") then
            return nil
        end

        local sdkroot = path.join(os.projectdir(), "Engine", "ThirdParty", "VulkanSDK")
        local versiondirs = os.dirs(path.join(sdkroot, "*"))
        if not versiondirs or #versiondirs == 0 then
            return nil
        end

        table.sort(versiondirs)
        for index = #versiondirs, 1, -1 do
            local sdkdir = path.join(versiondirs[index], "macOS")
            local header = path.join(sdkdir, "include", "vulkan", "vulkan.h")
            local dylib = path.join(sdkdir, "lib", "libvulkan.dylib")
            if os.isfile(header) and os.isfile(dylib) then
                return sdkdir
            end
        end
        return nil
    end

    local function _sdkinfo(sdkdir)
        if not sdkdir then
            return nil
        end
        return {
            sdkdir = sdkdir,
            bindir = path.join(sdkdir, "bin"),
            includedirs = { path.join(sdkdir, "include") },
            linkdirs = { path.join(sdkdir, "lib") },
            icd = path.join(sdkdir, "share", "vulkan", "icd.d", "MoltenVK_icd.json"),
            layerdir = path.join(sdkdir, "share", "vulkan", "explicit_layer.d")
        }
    end

    local function _result_from_sdkinfo(package, sdkinfo, find_library)
        if not sdkinfo then
            return nil
        end

        local result = {
            includedirs = sdkinfo.includedirs,
            linkdirs = sdkinfo.linkdirs,
            links = {}
        }

        local utils = {}
        for _, util in ipairs(table.wrap(package:config("utils"))) do
            table.insert(utils, util)
        end
        table.insert(utils, package:is_plat("windows", "mingw") and "vulkan-1" or "vulkan")

        for _, util in ipairs(utils) do
            if not find_library(util, sdkinfo.linkdirs, { plat = package:plat() }) then
                wprint(format("The library %s for %s is not found!", util, package:arch()))
                return nil
            end
            table.insert(result.links, util)
        end
        return result
    end

    on_load(function(package)
        local sdkinfo = _sdkinfo(_repo_local_sdkdir())
        if not sdkinfo then
            import("detect.sdks.find_vulkansdk")
            sdkinfo = find_vulkansdk()
        end
        if not sdkinfo then
            return
        end

        if sdkinfo.sdkdir then
            package:setenv("VULKAN_SDK", sdkinfo.sdkdir)
        end
        if sdkinfo.bindir then
            package:addenv("PATH", sdkinfo.bindir)
        end

        if is_host("macosx") and sdkinfo.linkdirs and sdkinfo.linkdirs[1] then
            local sdkdir = sdkinfo.sdkdir or path.directory(sdkinfo.linkdirs[1])
            package:addenv("DYLD_LIBRARY_PATH", sdkinfo.linkdirs[1])
            if sdkdir then
                package:setenv("VK_ICD_FILENAMES", path.join(sdkdir, "share", "vulkan", "icd.d", "MoltenVK_icd.json"))
                package:setenv("VK_LAYER_PATH", path.join(sdkdir, "share", "vulkan", "explicit_layer.d"))
            end
        end
    end)

    on_fetch(function(package, opt)
        if opt.system then
            import("lib.detect.find_library")

            local sdkinfo = _sdkinfo(_repo_local_sdkdir())
            if sdkinfo then
                return _result_from_sdkinfo(package, sdkinfo, find_library)
            end

            import("detect.sdks.find_vulkansdk")
            local vulkansdk = find_vulkansdk()
            if vulkansdk then
                return _result_from_sdkinfo(package, {
                    includedirs = vulkansdk.includedirs,
                    linkdirs = vulkansdk.linkdirs
                }, find_library)
            end
        end
    end)
end
