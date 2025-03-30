#pragma once

#include <filesystem>
#include <memory>
#include <optional>
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
    std::unordered_map<std::string, stdpath> pluginRoots;
    std::unordered_map<std::string, stdpath> mountRoots;

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
    const std::unordered_map<std::string, stdpath> &getPluginRoots() const { return pluginRoots; }
    const std::unordered_map<std::string, stdpath> &getMountRoots() const { return mountRoots; }
};