#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ya
{

struct FPluginDescriptor
{
    uint32_t                           schemaVersion = 1;
    std::string                        name;
    std::vector<std::filesystem::path> modules;
    std::vector<std::filesystem::path> contentDirs;
    std::vector<std::filesystem::path> configFiles;
    std::filesystem::path              sourcePath;

    [[nodiscard]] static FPluginDescriptor load(const std::filesystem::path& path);
};

} // namespace ya
