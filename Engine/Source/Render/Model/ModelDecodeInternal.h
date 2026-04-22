#pragma once

#include "Render/Model.h"

#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

namespace ya::model_decode
{

DecodedModelData decodeWithTinyGltf(const std::string& filepath);
DecodedModelData decodeWithAssimp(const std::string& filepath);

inline bool containsInsensitive(std::string_view text, std::string_view token)
{
    return std::search(
               text.begin(), text.end(), token.begin(), token.end(), [](char lhs, char rhs)
               { return static_cast<char>(std::tolower(static_cast<unsigned char>(lhs))) ==
                        static_cast<char>(std::tolower(static_cast<unsigned char>(rhs))); }) != text.end();
}

inline bool isGltfPath(const std::string& filepath)
{
    std::string extension = std::filesystem::path(filepath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".gltf" || extension == ".glb";
}

inline CoordinateSystem inferCoordSystem(const std::string& filepath)
{
    std::string extension = std::filesystem::path(filepath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
        extension == ".dae" || extension == ".collada" || extension == ".blend" || extension == ".3ds" ||
        extension == ".max" || extension == ".stl") {
        return CoordinateSystem::RightHanded;
    }

    YA_CORE_WARN("Unknown model format '{}', assuming RightHanded", extension);
    return CoordinateSystem::RightHanded;
}

inline void setTextureAlias(MaterialData& matData, const FName& primary, const FName& alias, const std::string& path)
{
    if (path.empty()) {
        return;
    }

    matData.setTexturePath(primary, path);
    matData.setTexturePath(alias, path);
}

} // namespace ya::model_decode
