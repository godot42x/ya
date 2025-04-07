#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "Render/Texture.h"

struct CommandBuffer;

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // Default white color
};

struct Mesh
{
    std::vector<Vertex>      vertices;
    std::vector<uint32_t>    indices;
    std::string              name;
    std::shared_ptr<Texture> diffuseTexture = nullptr;

    Mesh()  = default;
    ~Mesh() = default;
};

class Model
{
  private:
    std::vector<Mesh> meshes;
    glm::mat4         transform = glm::mat4(1.0f);

    bool        isLoaded = false;
    std::string directory;

  public:
    Model()  = default;
    ~Model() = default;

    bool loadFromOBJ(const std::string &filePath, std::shared_ptr<CommandBuffer> commandBuffer);
    void draw(SDL_GPURenderPass *renderPass, SDL_GPUTexture *defaultTexture);

    const std::vector<Mesh> &getMeshes() const { return meshes; }
    std::vector<Mesh>       &getMeshes() { return meshes; } // Non-const version for adding meshes

    glm::mat4 getTransform() const { return transform; }
    void      setTransform(const glm::mat4 &transform) { this->transform = transform; }

    bool getIsLoaded() const { return isLoaded; }
    void setIsLoaded(bool loaded) { isLoaded = loaded; }

    const std::string &getDirectory() const { return directory; }
    void               setDirectory(const std::string &directory) { this->directory = directory; }
};
