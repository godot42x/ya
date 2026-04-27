#pragma once

#include "Core/Log.h"
#include "Render/Model/ImportedModelData.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
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

inline void setTextureAlias(MaterialData& matData, const FName& primary, const FName& alias, const std::string& path)
{
    if (path.empty()) {
        return;
    }

    matData.setTexturePath(primary, path);
    matData.setTexturePath(alias, path);
}

} // namespace ya::model_importer::detail
