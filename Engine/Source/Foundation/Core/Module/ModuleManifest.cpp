#include "Foundation/Core/Module/ModuleManifest.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ya
{
namespace
{
EModuleKind parseModuleKind(const std::string& text)
{
    if (text == "runtime") {
        return EModuleKind::Runtime;
    }
    if (text == "project") {
        return EModuleKind::Project;
    }
    if (text == "editor") {
        return EModuleKind::Editor;
    }
    throw std::runtime_error("Unknown module kind: " + text);
}
} // namespace

FModuleManifest FModuleManifest::load(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open module manifest: " + path.string());
    }

    const auto json = nlohmann::json::parse(stream);
    FModuleManifest manifest;
    manifest.sourcePath   = std::filesystem::absolute(path).lexically_normal();
    manifest.schemaVersion = json.value("schemaVersion", 0u);
    manifest.name          = json.value("name", "");
    manifest.binary        = json.value("binary", "");
    manifest.kind          = parseModuleKind(json.value("kind", ""));

    if (manifest.schemaVersion != 1) {
        throw std::runtime_error("Unsupported module manifest schema: " + std::to_string(manifest.schemaVersion));
    }
    if (manifest.name.empty() || manifest.binary.empty()) {
        throw std::runtime_error("Module manifest requires non-empty name and binary: " + path.string());
    }

    for (const auto& dependency : json.value("dependencies", nlohmann::json::array())) {
        FModuleDependency value{
            .name     = dependency.value("name", ""),
            .required = dependency.value("required", true),
        };
        if (value.name.empty()) {
            throw std::runtime_error("Module dependency requires a name: " + path.string());
        }
        manifest.dependencies.push_back(std::move(value));
    }
    return manifest;
}

const char* moduleKindName(EModuleKind kind)
{
    switch (kind) {
    case EModuleKind::Runtime:
        return "runtime";
    case EModuleKind::Project:
        return "project";
    case EModuleKind::Editor:
        return "editor";
    }
    return "unknown";
}

} // namespace ya

