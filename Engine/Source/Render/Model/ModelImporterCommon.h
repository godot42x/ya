#pragma once

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"
#include "Render/Model/ImportedModelData.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace ya::model_importer::detail
{

inline bool containsInsensitive(std::string_view text, std::string_view token)
{
    return std::search(
               text.begin(), text.end(), token.begin(), token.end(), [](char lhs, char rhs)
               { return static_cast<char>(std::tolower(static_cast<unsigned char>(lhs))) ==
                        static_cast<char>(std::tolower(static_cast<unsigned char>(rhs))); }) != text.end();
}

inline std::string getNormalizedModelExtension(const std::string& filepath)
{
    std::string extension = std::filesystem::path(filepath).extension().string();
    std::ranges::transform(extension,
                           extension.begin(),
                           [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

inline bool isGltfPath(const std::string& filepath)
{
    const std::string extension = getNormalizedModelExtension(filepath);
    return extension == ".gltf" || extension == ".glb";
}

inline std::string normalizeImportedPathString(std::string_view path)
{
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const auto mountSeparator = normalized.find(':');
    if (mountSeparator != std::string::npos && normalized.find('/') > mountSeparator) {
        const auto mountName = normalized.substr(0, mountSeparator + 1);
        const auto tail      = std::filesystem::path(normalized.substr(mountSeparator + 1)).lexically_normal();
        return mountName + tail.generic_string();
    }

    return std::filesystem::path(normalized).lexically_normal().generic_string();
}

inline std::string normalizeImportedAssetPath(std::string_view path)
{
    return AssetManager::normalizeAssetPath(normalizeImportedPathString(path));
}

inline std::string resolveImportedIoPath(std::string_view path)
{
    const auto normalizedPath = normalizeImportedAssetPath(path);
    if (auto* vfs = VirtualFileSystem::get()) {
        const auto translatedPath = vfs->translatePath(normalizedPath);
        if (!translatedPath.empty()) {
            return translatedPath.lexically_normal().generic_string();
        }
    }

    return std::filesystem::path(normalizedPath).lexically_normal().generic_string();
}

inline bool isMountedAssetPath(std::string_view path)
{
    const auto separator = path.find(':');
    return separator != std::string::npos && path.find_first_of("/\\") > separator;
}

inline bool isProjectRelativeAssetPath(std::string_view path)
{
    return path == "Content" || path.starts_with("Content/") ||
           path == "Engine" || path.starts_with("Engine/");
}

inline CoordinateSystem inferCoordSystemFromExtensionHeuristic(const std::string& filepath)
{
    const std::string extension = getNormalizedModelExtension(filepath);

    if (extension == ".gltf" || extension == ".glb" ||
        extension == ".obj" || extension == ".dae" || extension == ".collada" ||
        extension == ".blend" || extension == ".3ds" || extension == ".max" || extension == ".stl") {
        return CoordinateSystem::RightHanded;
    }

    YA_CORE_WARN("Unknown handedness for model format '{}', assuming engine coordinate system", extension);
    return ENGINE_COORDINATE_SYSTEM;
}

inline std::string resolveImportedAssetPath(std::string_view directory, std::string_view rawPath)
{
    if (rawPath.empty()) {
        return {};
    }

    const auto normalizedRawPath = normalizeImportedPathString(rawPath);
    const std::filesystem::path sourcePath(normalizedRawPath);
    if (sourcePath.is_absolute()) {
        return AssetManager::normalizeAssetPath(sourcePath.generic_string());
    }

    if (isMountedAssetPath(normalizedRawPath) || isProjectRelativeAssetPath(normalizedRawPath)) {
        return AssetManager::normalizeAssetPath(normalizedRawPath);
    }

    const auto normalizedDirectory = normalizeImportedPathString(directory);
    if (!normalizedDirectory.empty() &&
        (normalizedRawPath == normalizedDirectory || normalizedRawPath.starts_with(normalizedDirectory + "/"))) {
        return AssetManager::normalizeAssetPath(normalizedRawPath);
    }

    const auto resolvedPath = (std::filesystem::path(normalizedDirectory) / sourcePath).lexically_normal();
    return AssetManager::normalizeAssetPath(resolvedPath.generic_string());
}

inline void setTextureAlias(MaterialData& matData, const FName& primary, const FName& alias, const std::string& path)
{
    if (path.empty()) {
        return;
    }

    const auto resolvedPath = resolveImportedAssetPath(matData.directory, path);
    if (resolvedPath.empty()) {
        return;
    }

    matData.setTexturePath(primary, resolvedPath);
    matData.setTexturePath(alias, resolvedPath);
}

} // namespace ya::model_importer::detail
