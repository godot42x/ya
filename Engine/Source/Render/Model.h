#pragma once

#include "Core/Base.h"

#include "Render/Mesh.h"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>


namespace ya
{
struct CommandBuffer;

struct ModelVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // Default white color
};

struct MeshData
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t>    indices;
    std::string              name;
    CoordinateSystem         sourceCoordSystem = CoordinateSystem::RightHanded; // Default for imported models

    stdptr<Mesh> mesh = nullptr;

    MeshData()  = default;
    ~MeshData() = default;

    // TODO: only temp data here
    void createGPUResources();
};

class Model
{
  private:
    std::vector<MeshData> meshes;
    glm::mat4             transform = glm::mat4(1.0f);

    bool        isLoaded = false;
    std::string directory;

  public:
    Model()  = default;
    ~Model() = default;

    void draw(SDL_GPURenderPass *renderPass, SDL_GPUTexture *defaultTexture);

    const std::vector<MeshData> &getMeshes() const { return meshes; }
    std::vector<MeshData>       &getMeshes() { return meshes; } // Non-const version for adding meshes

    glm::mat4 getTransform() const { return transform; }
    void      setTransform(const glm::mat4 &transform) { this->transform = transform; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    const std::string &getDirectory() const { return directory; }
    void               setDirectory(const std::string &directory) { this->directory = directory; }
};

} // namespace ya