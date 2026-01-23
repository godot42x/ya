#pragma once

#include "Core/System/System.h"

#include "Core/Delegate.h"
#include "Core/Log.h"
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
    static VirtualFileSystem *instance;

    stdpath projectRoot;
    stdpath engineRoot;
    stdpath gameRoot; // Current game/example root


    std::unordered_map<std::string, stdpath> mountPoints;  // Virtual path -> Physical path mapping
    std::unordered_map<std::string, stdpath> pluginMounts; // Virtual path -> Physical path mapping

    Delegate<void(const std::string &filepath)>                     onFileAlreadyExistsOnSave;
    Delegate<void(const std::string &filepath, size_t bytesLoaded)> onFileLoaded;

  public:
    static void               init();
    static VirtualFileSystem *get() { return instance; }

    VirtualFileSystem()
    {
        projectRoot = std::filesystem::current_path();
        engineRoot  = projectRoot / "Engine";
        mount("Engine", engineRoot);
    }

    const stdpath &getEngineRoot() const { return engineRoot; }
    const stdpath &getProjectRoot() const { return projectRoot; }
    const stdpath &getGameRoot() const { return gameRoot; }

    const auto &getMountPoints() const { return mountPoints; }

    std::optional<stdpath> getMountPoint(const std::string &mountName) const
    {
        auto it = mountPoints.find(mountName);
        if (it == mountPoints.end()) return std::nullopt;
        return it->second;
    }


    // Set the active game root (should be called from game entry point)
    void setGameRoot(const stdpath &path)
    {
        gameRoot = path;
        mount("Game", path);
    }

    // Register custom mount point: "MyData" -> "path/to/data"
    void mount(const std::string &mountName, const stdpath &physicalPath)
    {
        mountPoints[mountName] = physicalPath;
        YA_CORE_INFO("VirtualFileSystem::mount - Mounted {} -> {}", mountName, physicalPath.string());
    }

    void mountPlugin(const std::string &mountName, const stdpath &physicalPath)
    {
        pluginMounts[mountName] = physicalPath;
        mount(mountName, physicalPath);
        YA_CORE_INFO("VirtualFileSystem::mountPlugin - Mounted {} -> {}", mountName, physicalPath.string());
    }
    void unmountPlugin(const std::string &mountName)
    {
        pluginMounts.erase(mountName);
    }

    [[nodiscard]] auto getAllConentDir() const
    {
        std::unordered_map<std::string, stdpath> ret;
        for (auto &[n, p] : mountPoints)
        {
            if (std::filesystem::is_directory(p / "Content")) {
                ret.insert({n, p / "Content"});
            }
        }
        return ret;
    }


    // Unmount a mount point
    void unmount(const std::string &mountName)
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

    stdpath translatePath(std::string_view virtualPath) const
    {
        auto index = virtualPath.find_first_of(":");
        if (index == std::string_view::npos) {

            return projectRoot / virtualPath;
        }

        auto mountName    = virtualPath.substr(0, index);
        auto physicalPath = virtualPath.substr(index + 1);

        auto it = mountPoints.find(std::string(mountName));
        if (it == mountPoints.end()) {
            YA_CORE_ERROR("VirtualFileSystem::translatePath - Mount point not found: {}", mountName);
            return {};
        }
        return it->second / physicalPath;
    }


    std::vector<uint8_t> loadFileToMemory(std::string_view filepath) const;
    bool                 readFileToString(std::string_view filepath, std::string &output) const;

    bool isFileExists(const std::string &filepath) const
    {
        return std::filesystem::exists(translatePath(filepath));
    }
    bool isDirectoryExists(const std::string &filepath) const
    {
        return std::filesystem::is_directory(translatePath(filepath));
    }

    void saveToFile(std::string_view filepath, const std::string &data) const
    {
        auto fp = stdpath(filepath);

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