#include "App/Module/ProjectDescriptor.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ya
{
namespace
{

/// Descriptor-relative values are project-relative first, then
/// workspace-relative (`Engine/Content/...`, `Example/...`), per the contract
/// on the icon field. The workspace is read from the VFS ("Engine" mount's
/// parent), not from the process CWD: the editor's project browser opens
/// descriptors no matter how the process was booted, and the CWD is only the
/// launcher's way of keeping the VFS honest.
std::filesystem::path resolveProjectPath(const std::filesystem::path& projectRoot,
                                         const std::filesystem::path& value)
{
    if (value.empty()) {
        return {};
    }
    if (value.is_absolute()) {
        return value.lexically_normal();
    }

    const auto projectRelative = (projectRoot / value).lexically_normal();
    if (std::filesystem::exists(projectRelative)) {
        return projectRelative;
    }
    if (const auto* vfs = VirtualFileSystem::get()) {
        if (const auto engineRoot = vfs->getMountPoint("Engine")) {
            const auto workspaceRelative = (engineRoot->parent_path() / value).lexically_normal();
            if (std::filesystem::exists(workspaceRelative)) {
                return workspaceRelative;
            }
        }
    }
    // First bootstrap load (Entry.cpp) runs before the VFS exists; the launcher
    // contract keeps the CWD at the workspace root for that one.
    if (std::filesystem::exists(value)) {
        return std::filesystem::absolute(value).lexically_normal();
    }
    return projectRelative;
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
    // One canonical physical form regardless of how the path was provided
    // (symlinked temp dirs: /var -> /private/var on macOS) -- same invariant as
    // the VFS mounts, so project facts compare equal across entry routes.
    std::error_code canonicalError;
    const auto canonicalPath = std::filesystem::weakly_canonical(std::filesystem::absolute(path), canonicalError);
    descriptor.sourcePath    = (canonicalError ? std::filesystem::absolute(path) : canonicalPath).lexically_normal();
    descriptor.schemaVersion = json.value("schemaVersion", 0u);
    descriptor.name          = json.value("name", "");
    descriptor.mainModule    = json.value("mainModule", "");
    descriptor.contentDir    = json.value("contentDir", "Content");
    if (json.contains("defaultScene")) {
        descriptor.defaultScene = json.at("defaultScene").get<std::string>();
    }
    if (json.contains("uiReferenceResolution")) {
        const auto& value = json.at("uiReferenceResolution");
        if (!value.is_array() || value.size() != 2) {
            throw std::runtime_error("Project uiReferenceResolution must be [width, height]: " + path.string());
        }
        FUIReferenceResolution resolution{
            .width  = value.at(0).get<uint32_t>(),
            .height = value.at(1).get<uint32_t>(),
        };
        if (resolution.width == 0 || resolution.height == 0) {
            throw std::runtime_error("Project uiReferenceResolution must be non-zero: " + path.string());
        }
        descriptor.uiReferenceResolution = resolution;
    }
    if (json.contains("icon")) {
        const auto icon = json.at("icon").get<std::string>();
        if (!icon.empty()) {
            descriptor.icon = icon;
        }
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
        // Store the resolved fact, not the raw form: consumers (openProject,
        // startup scene, the editor) load through it without re-resolving
        // against the process CWD.
        const auto resolvedDefaultScene = resolveProjectPath(root, *descriptor.defaultScene);
        if (!std::filesystem::is_regular_file(resolvedDefaultScene)) {
            throw std::runtime_error("Project defaultScene not found: " + resolvedDefaultScene.string());
        }
        descriptor.defaultScene = resolvedDefaultScene.string();
    }
    if (descriptor.icon) {
        const auto resolvedIcon = resolveProjectPath(root, *descriptor.icon);
        if (!std::filesystem::is_regular_file(resolvedIcon)) {
            throw std::runtime_error("Project icon not found: " + resolvedIcon.string());
        }
        descriptor.icon = resolvedIcon.string();
    }
    return descriptor;
}

std::filesystem::path FProjectDescriptor::resolvePath(const std::filesystem::path& value) const
{
    return resolveProjectPath(sourcePath.parent_path(), value);
}

} // namespace ya
