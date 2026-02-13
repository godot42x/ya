#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/Math/AABB.h"
#include "Render/Mesh.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>


namespace ya
{

/**
 * @brief Vertex format for imported 3D models
 * Used during model loading, converted to engine's internal Vertex format
 */
struct ModelVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};


struct ModalTexture
{
    std::string type;
    std::string path;
};

// ========================================
// MaterialData - Dynamic material descriptor
// ========================================

/**
 * @brief Variant type for material parameter values
 * Supports common material data types
 */
using MaterialValue = std::variant<
    bool,
    int,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string>;

/**
 * @brief Standard parameter keys (conventions)
 * Using FName for efficient comparison and hashing
 */
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
inline const FName AlphaCutoff  = "alphaCutoff";
inline const FName RefractIndex = "refractIndex";
inline const FName Reflection   = "reflection";
inline const FName DoubleSided  = "doubleSided";
} // namespace MatParam

/**
 * @brief Standard texture slot keys
 * Using FName for efficient comparison and hashing
 */
namespace MatTexture
{
inline const FName Diffuse   = "diffuse";
inline const FName Albedo    = "albedo"; // PBR alias for diffuse
inline const FName Specular  = "specular";
inline const FName Normal    = "normal";
inline const FName Emissive  = "emissive";
inline const FName Metallic  = "metallic";
inline const FName Roughness = "roughness";
inline const FName AO        = "ao";
} // namespace MatTexture

/**
 * @brief Generic material data extracted from model files
 *
 * Designed to be material-type agnostic (Phong, PBR, Toon, etc.)
 * Each material component knows how to import from this descriptor.
 *
 * Replaces the old EmbeddedMaterial with a more flexible design:
 * - Dynamic parameters via std::variant
 * - Dynamic texture paths via map
 * - Easy to extend without modifying struct definition
 */
struct MaterialData
{
    std::string name;
    std::string type;      // "phong", "pbr", "unlit", etc. (hint for components)
    std::string directory; // Base directory for resolving relative texture paths

    // Dynamic parameters (colors, floats, bools, etc.)
    // Key: FName for efficient lookup
    std::unordered_map<FName, MaterialValue> params;

    // Texture paths (relative to model directory)
    // Key: FName for efficient lookup
    std::unordered_map<FName, std::string> texturePaths;

    // ========================================
    // Helper accessors with type safety
    // ========================================

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

    /**
     * @brief Resolve a relative texture path to absolute
     */
    std::string resolveTexturePath(const FName& key) const
    {
        std::string texPath = getTexturePath(key);
        if (texPath.empty()) {
            return "";
        }
        // If already absolute, use as-is
        if (texPath.find(':') != std::string::npos || texPath[0] == '/') {
            return texPath;
        }
        // Make relative to model directory
        return directory + texPath;
    }
};

// Type alias for backward compatibility (deprecation notice)
using EmbeddedMaterial = MaterialData;

struct MeshData
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t>    indices;


    stdptr<Mesh> createMesh(const std::string& meshName, CoordinateSystem sourceCoordSystem)
    {
        std::vector<ya::Vertex> engineVertices;
        for (const auto& v : vertices) {
            ya::Vertex vertex;
            vertex.position  = v.position;
            vertex.normal    = v.normal;
            vertex.texCoord0 = v.texCoord;
            engineVertices.push_back(vertex);
        }

        // Create Mesh GPU resource directly
        auto newMesh = makeShared<Mesh>(engineVertices, indices, meshName, sourceCoordSystem);
        return newMesh;
    }
};
/**
 * @brief Model resource - file-level asset container
 * Represents a loaded 3D model file (.obj, .fbx, .gltf, etc.)
 * One model can contain multiple meshes (e.g., character = head + body + weapon)
 *
 * Responsibility:
 * - Asset management (loading, caching)
 * - Mesh collection
 * - Embedded material storage
 * - Metadata (filepath, bounds)
 *
 * NOT responsible for:
 * - Runtime material instances (managed by MaterialComponent)
 * - Transforms (managed by TransformComponent)
 * - Rendering (handled by render systems)
 */
struct Model
{
    std::string name;
    std::string filepath;
    std::string directory;

    std::vector<stdptr<Mesh>> meshes;      // GPU geometry resources
    AABB                      boundingBox; // Overall bounding box

    bool isLoaded = false;

    // ========================================
    // Material Data (imported from model file)
    // ========================================

    /**
     * @brief Materials extracted from the model file
     * Index corresponds to material index in the original file
     */
    std::vector<MaterialData> embeddedMaterials;

    /**
     * @brief Mesh to material mapping
     * meshMaterialIndices[meshIndex] = materialIndex
     * -1 means no material assigned
     */
    std::vector<int32_t> meshMaterialIndices;

  public:
    Model()  = default;
    ~Model() = default;

    // Accessors
    const std::vector<stdptr<Mesh>>& getMeshes() const { return meshes; }
    std::vector<stdptr<Mesh>>&       getMeshesMut() { return meshes; }

    size_t       getMeshCount() const { return meshes.size(); }
    stdptr<Mesh> getMesh(size_t index) const { return index < meshes.size() ? meshes[index] : nullptr; }

    const std::string& getFilepath() const { return filepath; }
    const std::string& getDirectory() const { return directory; }
    const std::string& getName() const { return name; }

    void setDirectory(const std::string& dir) { directory = dir; }
    void setFilepath(const std::string& path) { filepath = path; }
    void setName(const std::string& n) { name = n; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    // ========================================
    // Material Data Accessors
    // ========================================

    /**
     * @brief Get the material data for a specific mesh
     * @param meshIndex Index of the mesh
     * @return Pointer to material data, or nullptr if not assigned
     */
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

    /**
     * @brief Get all material data
     */
    const std::vector<MaterialData>& getEmbeddedMaterials() const { return embeddedMaterials; }

    /**
     * @brief Get the material index for a specific mesh
     * @return Material index, or -1 if not assigned
     */
    int32_t getMaterialIndex(size_t meshIndex) const
    {
        return meshIndex < meshMaterialIndices.size() ? meshMaterialIndices[meshIndex] : -1;
    }
};

} // namespace ya