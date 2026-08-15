#include "App/Module/PluginDescriptor.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ya
{

FPluginDescriptor FPluginDescriptor::load(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open plugin descriptor: " + path.string());
    }

    const auto json = nlohmann::json::parse(stream);
    FPluginDescriptor descriptor;
    descriptor.sourcePath    = std::filesystem::absolute(path).lexically_normal();
    descriptor.schemaVersion = json.value("schemaVersion", 0u);
    descriptor.name          = json.value("name", "");

    if (descriptor.schemaVersion != 1) {
        throw std::runtime_error("Unsupported plugin descriptor schema: " + std::to_string(descriptor.schemaVersion));
    }
    if (descriptor.name.empty()) {
        throw std::runtime_error("Plugin descriptor requires a non-empty name: " + path.string());
    }

    const auto root = descriptor.sourcePath.parent_path();
    for (const auto& modulePath : json.value("modules", nlohmann::json::array())) {
        descriptor.modules.push_back((root / modulePath.get<std::string>()).lexically_normal());
    }
    for (const auto& contentDir : json.value("contentDirs", nlohmann::json::array())) {
        descriptor.contentDirs.push_back((root / contentDir.get<std::string>()).lexically_normal());
    }
    for (const auto& configFile : json.value("configFiles", nlohmann::json::array())) {
        descriptor.configFiles.push_back((root / configFile.get<std::string>()).lexically_normal());
    }

    if (descriptor.modules.empty() && descriptor.contentDirs.empty() && descriptor.configFiles.empty()) {
        throw std::runtime_error("Plugin descriptor requires at least one module, content dir, or config file: " + path.string());
    }
    return descriptor;
}

} // namespace ya
