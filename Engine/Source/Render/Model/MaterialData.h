#pragma once

#include "Core/FName.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <variant>

namespace ya
{

using MaterialValue = std::variant<
    bool,
    int,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string>;

namespace MatParam
{
inline const FName BaseColor    = "baseColor";
inline const FName Ambient      = "ambient";
inline const FName Specular     = "specular";
inline const FName Emissive     = "emissive";
inline const FName Shininess    = "shininess";
inline const FName Metallic     = "metallic";
inline const FName Roughness    = "roughness";
inline const FName Opacity      = "opacity";
inline const FName AlphaMode    = "alphaMode";
inline const FName AlphaCutoff  = "alphaCutoff";
inline const FName RefractIndex = "refractIndex";
inline const FName Reflection   = "reflection";
inline const FName DoubleSided  = "doubleSided";
} // namespace MatParam

namespace MatTexture
{
using T                              = FName;
inline const FName Diffuse           = "diffuse";
inline const FName Albedo            = "albedo";
inline const FName Specular          = "specular";
inline const FName Normal            = "normal";
inline const FName Emissive          = "emissive";
inline const FName Metallic          = "metallic";
inline const FName Roughness         = "roughness";
inline const FName MetallicRoughness = "metallicRoughness";
inline const FName AO                = "ao";
} // namespace MatTexture

struct MaterialData
{
    std::string name;
    std::string type;
    std::string directory;

    std::unordered_map<FName, MaterialValue> params;
    std::unordered_map<FName, std::string>   texturePaths;

    template <typename T>
    T getParam(const FName& key, const T& defaultValue = T{}) const
    {
        auto it = params.find(key);
        if (it != params.end()) {
            if (auto* val = std::get_if<T>(&it->second)) {
                return *val;
            }
        }
        return defaultValue;
    }

    template <typename T>
    void setParam(const FName& key, const T& value)
    {
        params[key] = value;
    }

    std::string getTexturePath(const FName& key) const
    {
        auto it = texturePaths.find(key);
        return it != texturePaths.end() ? it->second : "";
    }

    void setTexturePath(const FName& key, const std::string& path)
    {
        if (!path.empty()) {
            texturePaths[key] = path;
        }
    }

    bool hasParam(const FName& key) const
    {
        return params.find(key) != params.end();
    }

    bool hasTexture(const FName& key) const
    {
        auto it = texturePaths.find(key);
        return it != texturePaths.end() && !it->second.empty();
    }

    std::string resolveTexturePath(const FName& key) const
    {
        std::string texPath = getTexturePath(key);
        if (texPath.empty()) {
            return "";
        }
        if (texPath.find(':') != std::string::npos || texPath[0] == '/') {
            return texPath;
        }
        if (!directory.empty() && texPath.rfind(directory, 0) == 0) {
            return texPath;
        }
        return directory + texPath;
    }
};

using EmbeddedMaterial = MaterialData;

} // namespace ya
