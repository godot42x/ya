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

namespace
{

std::pair<ya::FName, std::filesystem::path> splitMountedPath(std::string_view normalizedPath)
{
    const auto mountSeparator = normalizedPath.find(':');
    if (mountSeparator != std::string::npos && normalizedPath.find('/') > mountSeparator) {
        return {
            ya::FName(normalizedPath.substr(0, mountSeparator)),
            std::filesystem::path(normalizedPath.substr(mountSeparator + 1)),
        };
    }

    const auto slashSeparator = normalizedPath.find('/');
    if (slashSeparator != std::string::npos) {
        return {
            ya::FName(normalizedPath.substr(0, slashSeparator)),
            std::filesystem::path(normalizedPath.substr(slashSeparator + 1)),
        };
    }

    return {ya::FName(normalizedPath), std::filesystem::path{}};
}

} // namespace

std::filesystem::path VirtualFileSystem::translatePath(std::string_view virtualPath) const
{
    std::string normalizedPath(virtualPath);
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    const stdpath inputPath(normalizedPath);
    if (inputPath.is_absolute()) {
        return inputPath.lexically_normal();
    }

    const auto [mountName, physicalPath] = splitMountedPath(normalizedPath);
    if (mountName.isValid()) {
        auto it = mountPoints.find(mountName);
        if (it != mountPoints.end()) {
            return (it->second / physicalPath).lexically_normal();
        }
    }

    return (workingRoot / stdpath(normalizedPath)).lexically_normal();
}

std::string VirtualFileSystem::toVfsPath(std::string_view path) const
{
    if (path.empty()) {
        return {};
    }

    auto normalized = std::filesystem::path(std::string(path)).lexically_normal().generic_string();
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const auto tryMakeMountRelative = [&](const std::filesystem::path& root) -> std::string {
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

    const auto [mountedName, mountedTail] = splitMountedPath(normalized);
    if (mountedName.isValid() && getMountPoint(mountedName).has_value()) {
        return normalized;
    }

    if (std::filesystem::path(normalized).is_absolute()) {
        size_t      bestMountLength = 0;
        std::string bestMountedPath;
        for (const auto& [mountName, mountRoot] : mountPoints) {
            const auto relative = tryMakeMountRelative(mountRoot);
            if (relative.empty() && normalized != mountRoot.generic_string()) {
                continue;
            }

            const size_t mountLength = mountRoot.generic_string().size();
            if (mountLength < bestMountLength) {
                continue;
            }

            bestMountLength = mountLength;
            bestMountedPath = std::string(mountName.c_str()) + ":";
            if (!relative.empty()) {
                bestMountedPath += relative;
            }
        }
        if (!bestMountedPath.empty()) {
            return bestMountedPath;
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
