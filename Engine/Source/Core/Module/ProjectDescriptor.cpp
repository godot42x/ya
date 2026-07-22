#include "Core/Module/ProjectDescriptor.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ya
{

FProjectDescriptor FProjectDescriptor::load(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open project descriptor: " + path.string());
    }

    const auto json = nlohmann::json::parse(stream);
    FProjectDescriptor descriptor;
    descriptor.sourcePath    = std::filesystem::absolute(path).lexically_normal();
    descriptor.schemaVersion = json.value("schemaVersion", 0u);
    descriptor.name          = json.value("name", "");
    descriptor.mainModule    = json.value("mainModule", "");
    descriptor.contentDir    = json.value("contentDir", "Content");
    if (json.contains("defaultScene")) {
        descriptor.defaultScene = json.at("defaultScene").get<std::string>();
    }

    if (descriptor.schemaVersion != 1) {
        throw std::runtime_error("Unsupported project descriptor schema: " + std::to_string(descriptor.schemaVersion));
    }
    if (descriptor.name.empty() || descriptor.mainModule.empty()) {
        throw std::runtime_error("Project descriptor requires non-empty name and mainModule: " + path.string());
    }

    const auto root = descriptor.sourcePath.parent_path();
    for (const auto& modulePath : json.value("modules", nlohmann::json::array())) {
        descriptor.modules.push_back((root / modulePath.get<std::string>()).lexically_normal());
    }
    for (const auto& pluginPath : json.value("plugins", nlohmann::json::array())) {
        descriptor.plugins.push_back((root / pluginPath.get<std::string>()).lexically_normal());
    }
    if (descriptor.modules.empty()) {
        throw std::runtime_error("Project descriptor requires at least one module manifest: " + path.string());
    }
    descriptor.contentDir = (root / descriptor.contentDir).lexically_normal();
    return descriptor;
}

} // namespace ya
