#pragma once

#include <filesystem>
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

    stdpath translatePath(std::string_view virtualPath) const
    {
        if (virtualPath.starts_with("Engine/")) {
            return engineRoot / std::string(virtualPath.substr(strlen("Engine/")));
        }
        // if (virtualPath.starts_with("Plugins/")) {
        //     size_t      slashPos = virtualPath.find('/', strlen("Plugins/"));
        //     std::string pluginName(virtualPath.substr(strlen("Plugins/"), slashPos - strlen("Plugins/")));
        //     auto        it = pluginRoots.find(pluginName);
        //     if (it != pluginRoots.end()) {
        //         return it->second / std::string(virtualPath.substr(slashPos + 1));
        //     }
        // }
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
};