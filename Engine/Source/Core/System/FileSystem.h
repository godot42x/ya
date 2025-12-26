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



struct FileSystem
{
    using stdpath = std::filesystem::path;

  private:
    static FileSystem *instance;

    stdpath                                  projectRoot;
    stdpath                                  engineRoot;
    stdpath                                  gameRoot; // Current game/example root
    std::unordered_map<std::string, stdpath> pluginRoots;
    std::unordered_map<std::string, stdpath> mountPoints; // Virtual path -> Physical path mapping

    Delegate<void(const std::string &filepath)>                     onFileAlreadyExistsOnSave;
    Delegate<void(const std::string &filepath, size_t bytesLoaded)> onFileLoaded;

  public:
    static void        init();
    static FileSystem *get() { return instance; }

    FileSystem()
    {
        projectRoot = std::filesystem::current_path();
        engineRoot  = projectRoot / "Engine";
    }

    const stdpath                                  &getEngineRoot() const { return engineRoot; }
    const stdpath                                  &getProjectRoot() const { return projectRoot; }
    const stdpath                                  &getGameRoot() const { return gameRoot; }
    const std::unordered_map<std::string, stdpath> &getPluginRoots() const { return pluginRoots; }
    const std::unordered_map<std::string, stdpath> &getMountPoints() const { return mountPoints; }

    // Set the active game root (should be called from game entry point)
    void setGameRoot(const stdpath &path)
    {
        gameRoot = path;
        // Auto-mount common game directories
        mountPoints["Content"] = gameRoot / "Content";
        mountPoints["Config"]  = gameRoot / "Config";
        mountPoints["Save"]    = gameRoot / "Save";
    }

    // Register custom mount point: "MyData" -> "path/to/data"
    void mount(const std::string &mountName, const stdpath &physicalPath)
    {
        mountPoints[mountName] = physicalPath;
    }

    // Unmount a mount point
    void unmount(const std::string &mountName)
    {
        mountPoints.erase(mountName);
    }

    stdpath translatePath(std::string_view virtualPath) const
    {
        // Engine resource: "Engine/Shader/Basic.glsl"
        if (virtualPath.starts_with("Engine/")) {
            return engineRoot / std::string(virtualPath.substr(strlen("Engine/")));
        }

        // Content priority: Game Content > Engine Content
        if (virtualPath.starts_with("Content/")) {
            // Try game root first
            if (!gameRoot.empty()) {
                stdpath gamePath = gameRoot / std::string(virtualPath);
                if (std::filesystem::exists(gamePath)) {
                    return gamePath;
                }
            }
            // Fallback to Engine/Content
            stdpath enginePath = engineRoot / std::string(virtualPath);
            if (std::filesystem::exists(enginePath)) {
                return enginePath;
            }
            // Return game path even if not exists (for write operations)
            if (!gameRoot.empty()) {
                return gameRoot / std::string(virtualPath);
            }
            return enginePath;
        }

        // Config/Save - game specific
        if ((virtualPath.starts_with("Config/") || virtualPath.starts_with("Save/")) && !gameRoot.empty()) {
            return gameRoot / std::string(virtualPath);
        }

        // Mount point: "MyData/config.json" -> custom mounted path
        size_t slashPos = virtualPath.find('/');
        if (slashPos != std::string_view::npos) {
            std::string mountName(virtualPath.substr(0, slashPos));
            auto        it = mountPoints.find(mountName);
            if (it != mountPoints.end()) {
                return it->second / std::string(virtualPath.substr(slashPos + 1));
            }
        }

        // Fallback: relative to project root
        return projectRoot / std::string(virtualPath);
    }

    std::vector<void *> loadFileToMemory(std::string_view filepath) const
    {
        // std::string fullPath = translatePath(filepath).string();
        // std::vector<void *> outData;
        // ut::file::read_all(fullPath, outData);
    }


    bool readFileToString(std::string_view filepath, std::string &output) const;
    bool isFileExists(const std::string &filepath) const
    {
        return std::filesystem::exists(projectRoot / filepath);
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
            YA_CORE_ERROR("FileSystem::saveToFile - Failed to open file for writing: {}", filepath);
            return;
        }
        file.write(data.data(), data.size());
        file.close();
    }
};