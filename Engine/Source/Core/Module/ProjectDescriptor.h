#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct FProjectDescriptor
{
    uint32_t                           schemaVersion = 1;
    std::string                        name;
    std::string                        mainModule;
    std::vector<std::filesystem::path> modules;
    std::vector<std::filesystem::path> plugins;
    std::filesystem::path              contentDir = "Content";
    std::optional<std::string>         defaultScene;
    std::unordered_map<std::string, std::vector<std::string>> inputActions;
    std::filesystem::path              sourcePath;

    [[nodiscard]] static FProjectDescriptor load(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path resolvePath(const std::filesystem::path& value) const;
};

} // namespace ya
