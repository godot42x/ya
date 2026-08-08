#include "Foundation/Core/Module/ProjectDescriptor.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ya
{
namespace
{

std::filesystem::path resolveProjectPath(const std::filesystem::path& projectRoot,
                                         const std::filesystem::path& value)
{
    if (value.empty()) {
        return {};
    }
    if (value.is_absolute()) {
        return value.lexically_normal();
    }
    if (std::filesystem::exists(value)) {
        return std::filesystem::absolute(value).lexically_normal();
    }
    return (projectRoot / value).lexically_normal();
}

}

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
    if (json.contains("inputActions")) {
        for (const auto& [actionName, actionBindings] : json.at("inputActions").items()) {
            if (!actionBindings.is_array()) {
                throw std::runtime_error("Project inputActions entry must be an array: " + actionName);
            }

            auto& bindings = descriptor.inputActions[actionName];
            for (const auto& binding : actionBindings) {
                bindings.push_back(binding.get<std::string>());
            }
        }
    }

    if (descriptor.schemaVersion != 1) {
        throw std::runtime_error("Unsupported project descriptor schema: " + std::to_string(descriptor.schemaVersion));
    }
    if (descriptor.name.empty() || descriptor.mainModule.empty()) {
        throw std::runtime_error("Project descriptor requires non-empty name and mainModule: " + path.string());
    }

    const auto root = descriptor.sourcePath.parent_path();
    for (const auto& modulePath : json.value("modules", nlohmann::json::array())) {
        const auto resolvedPath = (root / modulePath.get<std::string>()).lexically_normal();
        if (!std::filesystem::is_regular_file(resolvedPath)) {
            throw std::runtime_error("Project module manifest not found: " + resolvedPath.string());
        }
        descriptor.modules.push_back(resolvedPath);
    }
    for (const auto& pluginPath : json.value("plugins", nlohmann::json::array())) {
        const auto resolvedPath = (root / pluginPath.get<std::string>()).lexically_normal();
        if (!std::filesystem::is_regular_file(resolvedPath)) {
            throw std::runtime_error("Project plugin descriptor not found: " + resolvedPath.string());
        }
        descriptor.plugins.push_back(resolvedPath);
    }
    if (descriptor.modules.empty()) {
        throw std::runtime_error("Project descriptor requires at least one module manifest: " + path.string());
    }
    descriptor.contentDir = (root / descriptor.contentDir).lexically_normal();
    if (!std::filesystem::exists(descriptor.contentDir)) {
        throw std::runtime_error("Project content dir not found: " + descriptor.contentDir.string());
    }
    if (descriptor.defaultScene) {
        const auto resolvedDefaultScene = resolveProjectPath(root, *descriptor.defaultScene);
        if (!std::filesystem::is_regular_file(resolvedDefaultScene)) {
            throw std::runtime_error("Project defaultScene not found: " + resolvedDefaultScene.string());
        }
    }
    return descriptor;
}

std::filesystem::path FProjectDescriptor::resolvePath(const std::filesystem::path& value) const
{
    return resolveProjectPath(sourcePath.parent_path(), value);
}

} // namespace ya
