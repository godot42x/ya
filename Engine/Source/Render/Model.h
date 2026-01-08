#pragma once

#include "Core/Base.h"
#include "Math/AABB.h"
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

/**
 * @brief Model resource - file-level asset container
 * Represents a loaded 3D model file (.obj, .fbx, .gltf, etc.)
 * One model can contain multiple meshes (e.g., character = head + body + weapon)
 * 
 * Responsibility:
 * - Asset management (loading, caching)
 * - Mesh collection
 * - Metadata (filepath, bounds)
 * 
 * NOT responsible for:
 * - Materials (managed by MeshRendererComponent)
 * - Transforms (managed by TransformComponent)
 * - Rendering (handled by render systems)
 */
struct Model
{
    std::string name;
    std::string filepath;
    std::string directory;
    
    std::vector<stdptr<Mesh>> meshes;  // GPU geometry resources
    AABB boundingBox;                   // Overall bounding box
    
    bool isLoaded = false;

  public:
    Model() = default;
    ~Model() = default;

    // Accessors
    const std::vector<stdptr<Mesh>> &getMeshes() const { return meshes; }
    std::vector<stdptr<Mesh>>       &getMeshes() { return meshes; }
    
    size_t getMeshCount() const { return meshes.size(); }
    stdptr<Mesh> getMesh(size_t index) const { return index < meshes.size() ? meshes[index] : nullptr; }
    
    const std::string &getFilepath() const { return filepath; }
    const std::string &getDirectory() const { return directory; }
    const std::string &getName() const { return name; }
    
    void setDirectory(const std::string &dir) { directory = dir; }
    void setFilepath(const std::string &path) { filepath = path; }
    void setName(const std::string &n) { name = n; }
    
    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }
};

} // namespace ya