#include "VirtualFileSystem.h"
#include "Core/Log.h"
#include "utility.cc/file_utils.h"

VirtualFileSystem *VirtualFileSystem::instance = nullptr;

void VirtualFileSystem::init()
{
    instance = new VirtualFileSystem();
}

VirtualFileSystem* VirtualFileSystem::get()
{
    return instance;
}

std::filesystem::path VirtualFileSystem::translatePath(std::string_view virtualPath) const
{
    std::string normalizedPath(virtualPath);
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    const stdpath inputPath(normalizedPath);
    if (inputPath.is_absolute()) {
        return inputPath.lexically_normal();
    }

    auto index = normalizedPath.find_first_of(":");
    if (index == std::string::npos) {
        const auto relativePath = stdpath(normalizedPath);
        if (!contentRoot.empty() && (normalizedPath == "Content" || normalizedPath.starts_with("Content/"))) {
            return (contentRoot / relativePath).lexically_normal();
        }
        return (projectRoot / relativePath).lexically_normal();
    }

    auto mountName    = std::string_view(normalizedPath).substr(0, index);
    auto physicalPath = std::string_view(normalizedPath).substr(index + 1);

    if (mountName == "Game") {
        mountName = "Content";
    }

    auto it = mountPoints.find(std::string(mountName));
    if (it == mountPoints.end()) {
        YA_CORE_ERROR("VirtualFileSystem::translatePath - Mount point not found: {}", mountName);
        return {};
    }
    return (it->second / physicalPath).lexically_normal();
}

std::string VirtualFileSystem::toVfsPath(std::string_view path) const
{
    if (path.empty()) {
        return {};
    }

    auto normalized = std::filesystem::path(std::string(path)).lexically_normal().generic_string();
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const auto tryMakeProjectRelative = [&](const std::filesystem::path& root) -> std::string {
        if (root.empty()) {
            return {};
        }

        std::error_code ec;
        const auto absoluteInput = std::filesystem::absolute(std::filesystem::path(path), ec);
        if (ec) {
            return {};
        }

        const auto relative = std::filesystem::relative(absoluteInput.lexically_normal(), root, ec);
        if (ec || relative.empty()) {
            return {};
        }

        const auto relativeGeneric = relative.generic_string();
        if (relativeGeneric == ".." || relativeGeneric.starts_with("../")) {
            return {};
        }

        return relativeGeneric;
    };

    if (normalized.starts_with("Game:")) {
        normalized = "Content:" + normalized.substr(std::string("Game:").size());
    }

    if (normalized.starts_with("Engine:") || normalized.starts_with("Content:")) {
        return normalized;
    }
    if (normalized.starts_with("Engine/Content/") || normalized == "Engine/Content") {
        return std::string("Engine:") + normalized.substr(std::string("Engine/").size());
    }
    if (normalized.starts_with("Content/") || normalized == "Content") {
        return normalized;
    }

    if (std::filesystem::path(normalized).is_absolute()) {
        if (!contentRoot.empty()) {
            if (const auto contentRelative = tryMakeProjectRelative(contentRoot); !contentRelative.empty()) {
                if (contentRelative == "Content" || contentRelative.starts_with("Content/")) {
                    return contentRelative;
                }
            }
        }

        if (!engineRoot.empty()) {
            if (const auto engineRelative = tryMakeProjectRelative(engineRoot); !engineRelative.empty()) {
                if (engineRelative == "Content" || engineRelative.starts_with("Content/")) {
                    return std::string("Engine:") + engineRelative;
                }
            }
        }

        if (!projectRoot.empty()) {
            if (const auto projectRelative = tryMakeProjectRelative(projectRoot); !projectRelative.empty()) {
                return projectRelative;
            }
        }
    }

    return normalized;
}
std::vector<uint8_t> VirtualFileSystem::loadFileToMemory(std::string_view filepath) const
{
    std::string fullPath = translatePath(filepath).string();
    auto        ret      = ut::file::read_all(fullPath);
    if (!ret) {
        YA_CORE_ERROR("Failed to read file: {}", fullPath);
        return {};
    }
    std::string& data = ret.value();
    return std::vector<uint8_t>(data.begin(), data.end());
}


bool VirtualFileSystem::readFileToString(std::string_view filepath, std::string &output) const
{
    std::filesystem::path fullPath = translatePath(filepath);

    auto opt = ut::file::read_all(fullPath);
    if (!opt) {
        YA_CORE_ERROR("Failed to read file: {}", std::filesystem::absolute(fullPath).string());
        return false;
    }
    output = *opt;
    return true;
}
