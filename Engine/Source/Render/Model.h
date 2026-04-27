#pragma once

#include "Core/Base.h"
#include "Core/Math/AABB.h"
#include "Render/Mesh.h"
#include "Render/Skeleton.h"

namespace ya
{

// #define MAX_BONE_WEIGHTS 4

/**
 * @brief Vertex format for imported 3D models
 * Used during model loading, converted to engine's internal Vertex format
 */

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

struct Model
{
    std::string name;
    std::string filepath;
    std::string directory;

    std::vector<stdptr<Mesh>> meshes;
    AABB                      boundingBox;

    bool isLoaded = false;

    std::vector<MaterialData> embeddedMaterials;
    std::vector<int32_t>      meshMaterialIndices;

    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<int32_t>                   meshSkeletonIndices;

  public:
    Model()  = default;
    ~Model() = default;

    const std::vector<stdptr<Mesh>>& getMeshes() const { return meshes; }
    std::vector<stdptr<Mesh>>&       getMeshesMut() { return meshes; }

    size_t       getMeshCount() const { return meshes.size(); }
    stdptr<Mesh> getMesh(size_t index) const { return index < meshes.size() ? meshes[index] : nullptr; }

    const std::string& getFilepath() const { return filepath; }
    const std::string& getDirectory() const { return directory; }
    const std::string& getName() const { return name; }

    void setDirectory(const std::string& dir) { directory = dir; }
    void setFilepath(const std::string& path) { filepath = path; }
    void setName(const std::string& modelName) { name = modelName; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    bool            hasSkeleton() const { return !skeletons.empty(); }
    size_t          getSkeletonCount() const { return skeletons.size(); }
    const Skeleton* getSkeleton(size_t index = 0) const
    {
        return index < skeletons.size() ? skeletons[index].get() : nullptr;
    }
    std::shared_ptr<Skeleton> getSkeletonShared(size_t index = 0) const
    {
        return index < skeletons.size() ? skeletons[index] : nullptr;
    }
    const Skeleton* getSkeletonForMesh(size_t meshIndex) const
    {
        if (meshIndex >= meshSkeletonIndices.size()) {
            return nullptr;
        }
        const int32_t skeletonIndex = meshSkeletonIndices[meshIndex];
        if (skeletonIndex < 0 || skeletonIndex >= static_cast<int32_t>(skeletons.size())) {
            return nullptr;
        }
        return skeletons[static_cast<size_t>(skeletonIndex)].get();
    }

    const MaterialData* getMaterialForMesh(size_t meshIndex) const
    {
        if (meshIndex >= meshMaterialIndices.size()) {
            return nullptr;
        }
        int32_t matIndex = meshMaterialIndices[meshIndex];
        if (matIndex < 0 || matIndex >= static_cast<int32_t>(embeddedMaterials.size())) {
            return nullptr;
        }
        return &embeddedMaterials[matIndex];
    }

    const std::vector<MaterialData>& getEmbeddedMaterials() const { return embeddedMaterials; }

    int32_t getMaterialIndex(size_t meshIndex) const
    {
        return meshIndex < meshMaterialIndices.size() ? meshMaterialIndices[meshIndex] : -1;
    }

    int32_t getMeshSkeletonIndex(size_t meshIndex) const
    {
        return meshIndex < meshSkeletonIndices.size() ? meshSkeletonIndices[meshIndex] : -1;
    }
};


} // namespace ya
