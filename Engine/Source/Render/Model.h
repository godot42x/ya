#pragma once

#include "Core/Base.h"
#include "Core/Math/AABB.h"
#include "Render/Mesh.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
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

/**
 * @brief Embedded material data extracted from model file
 * 
 * Stores material information from the model file (e.g., FBX, glTF)
 * Used to initialize PhongMaterialComponent on child entities
 */
struct EmbeddedMaterial
{
    std::string name;

    // PBR-like parameters (compatible with Phong and PBR workflows)
    glm::vec4 baseColor   = {1.0f, 1.0f, 1.0f, 1.0f};  // Diffuse / Albedo
    glm::vec3 ambient     = {0.1f, 0.1f, 0.1f};
    glm::vec3 specular    = {0.5f, 0.5f, 0.5f};
    glm::vec3 emissive    = {0.0f, 0.0f, 0.0f};
    float     shininess   = 32.0f;
    float     metallic    = 0.0f;
    float     roughness   = 0.5f;
    float     opacity     = 1.0f;

    // Texture paths (relative to model directory)
    std::string diffuseTexturePath;
    std::string specularTexturePath;
    std::string normalTexturePath;
    std::string emissiveTexturePath;

    // Alpha mode
    enum class EAlphaMode : uint8_t { Opaque, Mask, Blend };
    EAlphaMode alphaMode   = EAlphaMode::Opaque;
    float      alphaCutoff = 0.5f;
    bool       doubleSided = false;
};


struct MeshData
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t>    indices;


    stdptr<Mesh> createMesh(const std::string &meshName, CoordinateSystem sourceCoordSystem)
    {
        std::vector<ya::Vertex> engineVertices;
        for (const auto &v : vertices) {
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
    // Embedded Material Data
    // ========================================

    /**
     * @brief Materials extracted from the model file
     * Index corresponds to material index in the original file
     */
    std::vector<EmbeddedMaterial> embeddedMaterials;

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
    const std::vector<stdptr<Mesh>> &getMeshes() const { return meshes; }
    std::vector<stdptr<Mesh>>       &getMeshesMut() { return meshes; }

    size_t       getMeshCount() const { return meshes.size(); }
    stdptr<Mesh> getMesh(size_t index) const { return index < meshes.size() ? meshes[index] : nullptr; }

    const std::string &getFilepath() const { return filepath; }
    const std::string &getDirectory() const { return directory; }
    const std::string &getName() const { return name; }

    void setDirectory(const std::string &dir) { directory = dir; }
    void setFilepath(const std::string &path) { filepath = path; }
    void setName(const std::string &n) { name = n; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    // ========================================
    // Embedded Material Accessors
    // ========================================

    /**
     * @brief Get the material for a specific mesh
     * @param meshIndex Index of the mesh
     * @return Pointer to embedded material, or nullptr if not assigned
     */
    const EmbeddedMaterial* getMaterialForMesh(size_t meshIndex) const
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
     * @brief Get all embedded materials
     */
    const std::vector<EmbeddedMaterial>& getEmbeddedMaterials() const { return embeddedMaterials; }

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