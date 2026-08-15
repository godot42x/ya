#pragma once

#include "Core/Api.h"
#include "Core/FName.h"
#include "Core/System/System.h"

#include "Core/Delegate.h"
#include "Core/Log.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


struct VirtualFileSystem;

namespace ya
{
using VFS = VirtualFileSystem;
}


struct VirtualFileSystem
{
    using stdpath = std::filesystem::path;

  private:
    static VirtualFileSystem* instance;

    /// One canonical physical form for roots/mounts so translatePath() output
    /// does not depend on how the path was provided (e.g. symlinked temp dirs:
    /// /var -> /private/var on macOS).
    static stdpath normalizePhysicalPath(const stdpath& path)
    {
        std::error_code ec;
        const stdpath canonicalPath = std::filesystem::weakly_canonical(path, ec);
        return ec ? path.lexically_normal() : canonicalPath;
    }

    stdpath workingRoot;

    std::unordered_map<ya::FName, stdpath> mountPoints;  // Virtual path -> Physical path mapping
    std::unordered_map<ya::FName, stdpath> pluginMounts; // Virtual path -> Physical path mapping

    Delegate<void(const std::string& filepath)>                     onFileAlreadyExistsOnSave;
    Delegate<void(const std::string& filepath, size_t bytesLoaded)> onFileLoaded;

  public:
    MulticastDelegate<void()> onMountPointChanged;

  public:
    static YA_CORE_API void              init();
    static YA_CORE_API VirtualFileSystem* get();

    VirtualFileSystem()
    {
        workingRoot = normalizePhysicalPath(std::filesystem::current_path());
    }

    const stdpath& getWorkingRoot() const { return workingRoot; }

    const auto& getMountPoints() const { return mountPoints; }

    std::optional<stdpath> getMountPoint(const ya::FName& mountName) const
    {
        auto it = mountPoints.find(mountName);
        if (it == mountPoints.end()) return std::nullopt;
        return it->second;
    }
    std::optional<stdpath> getMountPoint(std::string_view mountName) const
    {
        return getMountPoint(ya::FName(mountName));
    }
    std::optional<stdpath> getMountPoint(const char* mountName) const
    {
        return getMountPoint(std::string_view(mountName));
    }

    // Register custom mount point: "MyData" -> "path/to/data"
    void mount(const ya::FName& mountName, const stdpath& physicalPath)
    {
        mountPoints[mountName] = normalizePhysicalPath(physicalPath);
        YA_CORE_INFO("VirtualFileSystem::mount - Mounted {} -> {}", mountName, mountPoints[mountName].string());
        onMountPointChanged.broadcast();
    }
    void mount(std::string_view mountName, const stdpath& physicalPath)
    {
        mount(ya::FName(mountName), physicalPath);
    }
    void mount(const char* mountName, const stdpath& physicalPath)
    {
        mount(std::string_view(mountName), physicalPath);
    }

    void mountPlugin(const ya::FName& mountName, const stdpath& physicalPath)
    {
        pluginMounts[mountName] = physicalPath;
        mount(mountName, physicalPath);
        YA_CORE_INFO("VirtualFileSystem::mountPlugin - Mounted {} -> {}", mountName, physicalPath.string());
    }
    void mountPlugin(std::string_view mountName, const stdpath& physicalPath)
    {
        mountPlugin(ya::FName(mountName), physicalPath);
    }
    void mountPlugin(const char* mountName, const stdpath& physicalPath)
    {
        mountPlugin(std::string_view(mountName), physicalPath);
    }
    void unmountPlugin(const ya::FName& mountName)
    {
        pluginMounts.erase(mountName);
    }
    void unmountPlugin(std::string_view mountName)
    {
        unmountPlugin(ya::FName(mountName));
    }
    void unmountPlugin(const char* mountName)
    {
        unmountPlugin(std::string_view(mountName));
    }

    [[nodiscard]] auto getAllConentDir() const
    {
        std::unordered_map<std::string, stdpath> ret;
        for (auto& [n, p] : mountPoints)
        {
            if (n == ya::FName("Content")) {
                ret.insert({n.toString(), p});
            }
            else if (std::filesystem::is_directory(p / "Content")) {
                ret.insert({n.toString(), p / "Content"});
            }
        }
        return ret;
    }


    // Unmount a mount point
    void unmount(const ya::FName& mountName)
    {
        mountPoints.erase(mountName);
    }
    void unmount(std::string_view mountName)
    {
        unmount(ya::FName(mountName));
    }
    void unmount(const char* mountName)
    {
        unmount(std::string_view(mountName));
    }

    // from abs path to VFS mounted path?
    // stdpath resolvePath(std::string_view absPath) const
    // {
    //     auto abs = stdpath(absPath);
    //     if (abs.is_absolute() == false) {
    //         return abs;
    //     }

    //     if (abs.string().starts_with(projectRoot.string())) {
    //         return abs.relative_path();
    //     }

    //     return abs;
    // }

    auto relativeTo(std::string_view path, stdpath to) const
    {
        auto p = stdpath(path);
        return std::filesystem::relative(p, to);
    }

    YA_CORE_API stdpath translatePath(std::string_view virtualPath) const;
    YA_CORE_API std::string toVfsPath(std::string_view path) const;


    YA_CORE_API std::vector<uint8_t> loadFileToMemory(std::string_view filepath) const;
    YA_CORE_API bool                 readFileToString(std::string_view filepath, std::string& output) const;

    bool isFileExists(const std::string& filepath) const
    {
        return std::filesystem::exists(translatePath(filepath));
    }
    bool isDirectoryExists(const std::string& filepath) const
    {
        return std::filesystem::is_directory(translatePath(filepath));
    }

    void saveToFile(std::string_view filepath, const std::string& data) const
    {
        auto fp = translatePath(filepath);

        if (std::filesystem::is_directory(fp.parent_path()) == false)
        {
            std::filesystem::create_directories(fp.parent_path());
        }
        if (std::filesystem::exists(fp))
        {
            std::filesystem::remove(fp);
        }

        std::ofstream file(fp.c_str(), std::ios::binary);
        if (!file.is_open())
        {
            YA_CORE_ERROR("VirtualFileSystem::saveToFile - Failed to open file for writing: {}", filepath);
            return;
        }
        file.write(data.data(), data.size());
        file.close();
    }
};
