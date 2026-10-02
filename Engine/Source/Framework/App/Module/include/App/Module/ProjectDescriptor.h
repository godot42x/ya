#pragma once

#include "Core/Api.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Design size the game UI scales to fit (width and height, both non-zero).
/// Absent on a project means the UI lays out in viewport logical pixels.
struct FUIReferenceResolution
{
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct FProjectDescriptor
{
    uint32_t                           schemaVersion = 1;
    std::string                        name;
    std::string                        mainModule;
    std::vector<std::filesystem::path> modules;
    std::vector<std::filesystem::path> plugins;
    std::filesystem::path              contentDir = "Content";
    std::optional<std::string>            defaultScene;
    std::optional<FUIReferenceResolution> uiReferenceResolution;
    /// Dock / taskbar icon. PNG or BMP, project-relative or workspace-relative
    /// (`Engine/Content/...`). Omitted = engine YA branding.
    std::optional<std::string>         icon;
    std::unordered_map<std::string, std::vector<std::string>> inputActions;
    std::filesystem::path              sourcePath;

    [[nodiscard]] static YA_MODULE_MANAGER_API FProjectDescriptor load(const std::filesystem::path& path);
    [[nodiscard]] YA_MODULE_MANAGER_API std::filesystem::path resolvePath(const std::filesystem::path& value) const;
};

} // namespace ya
