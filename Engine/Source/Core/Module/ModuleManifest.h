#pragma once

#include "Core/Module/Module.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ya
{

struct FModuleDependency
{
    std::string name;
    bool        required = true;
};

struct FModuleManifest
{
    uint32_t                       schemaVersion = 1;
    std::string                    name;
    EModuleKind                    kind = EModuleKind::Runtime;
    std::filesystem::path          binary;
    std::vector<FModuleDependency> dependencies;
    std::filesystem::path          sourcePath;

    [[nodiscard]] static FModuleManifest load(const std::filesystem::path& path);
};

[[nodiscard]] const char* moduleKindName(EModuleKind kind);

} // namespace ya

