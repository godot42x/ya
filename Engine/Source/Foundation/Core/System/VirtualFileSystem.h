#pragma once

#include "Foundation/Core/Api.h"
#include "Foundation/Core/System/System.h"

#include "Foundation/Core/Delegate.h"
#include "Foundation/Core/Log.h"
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

    stdpath projectRoot;
    stdpath engineRoot;
    stdpath gameRoot;       // Current game/example root
    stdpath thirdPartyRoot; // Engine/ThirdParty


    std::unordered_map<std::string, stdpath> mountPoints;  // Virtual path -> Physical path mapping
    std::unordered_map<std::string, stdpath> pluginMounts; // Virtual path -> Physical path mapping

    Delegate<void(const std::string& filepath)>                     onFileAlreadyExistsOnSave;
    Delegate<void(const std::string& filepath, size_t bytesLoaded)> onFileLoaded;

  public:
    MulticastDelegate<void()> onMountPointChanged;

  public:
    static YA_CORE_API void              init();
    static YA_CORE_API VirtualFileSystem* get();

    VirtualFileSystem()
    {
        projectRoot = normalizePhysicalPath(std::filesystem::current_path());
        engineRoot  = projectRoot / "Engine";
        mount("Engine", engineRoot);
        thirdPartyRoot = engineRoot / "ThirdParty";
        mount("ThirdParty", thirdPartyRoot);
    }

    const stdpath& getEngineRoot() const { return engineRoot; }
    const stdpath& getProjectRoot() const { return projectRoot; }
    const stdpath& getGameRoot() const { return gameRoot; }

    const auto& getMountPoints() const { return mountPoints; }

    std::optional<stdpath> getMountPoint(const std::string& mountName) const
    {
        auto it = mountPoints.find(mountName);
        if (it == mountPoints.end()) return std::nullopt;
        return it->second;
    }


    // Set the active game root (should be called from game entry point)
    void setGameRoot(const stdpath& path)
    {
        gameRoot = normalizePhysicalPath(path);
        mount("Game", gameRoot);
    }

    // Register custom mount point: "MyData" -> "path/to/data"
    void mount(const std::string& mountName, const stdpath& physicalPath)
    {
        mountPoints[mountName] = normalizePhysicalPath(physicalPath);
        YA_CORE_INFO("VirtualFileSystem::mount - Mounted {} -> {}", mountName, mountPoints[mountName].string());
        onMountPointChanged.broadcast();
    }

    void mountPlugin(const std::string& mountName, const stdpath& physicalPath)
    {
        pluginMounts[mountName] = physicalPath;
        mount(mountName, physicalPath);
        YA_CORE_INFO("VirtualFileSystem::mountPlugin - Mounted {} -> {}", mountName, physicalPath.string());
    }
    void unmountPlugin(const std::string& mountName)
    {
        pluginMounts.erase(mountName);
    }

    [[nodiscard]] auto getAllConentDir() const
    {
        std::unordered_map<std::string, stdpath> ret;
        for (auto& [n, p] : mountPoints)
        {
            if (std::filesystem::is_directory(p / "Content")) {
                ret.insert({n, p / "Content"});
            }
        }
        return ret;
    }


    // Unmount a mount point
    void unmount(const std::string& mountName)
    {
        mountPoints.erase(mountName);
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
