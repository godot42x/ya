#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class Render;
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
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::string           name;
    SDL_GPUTexture       *diffuseTexture = nullptr;

    Mesh()  = default;
    ~Mesh() = default;
};

class Model
{
  public:
    Model()  = default;
    ~Model() = default;

    bool loadFromOBJ(const std::string &filePath, std::shared_ptr<CommandBuffer> commandBuffer);
    void draw(SDL_GPURenderPass *renderPass, SDL_GPUTexture *defaultTexture);

    const std::vector<Mesh> &getMeshes() const { return m_meshes; }
    glm::mat4                getTransform() const { return m_transform; }
    void                     setTransform(const glm::mat4 &transform) { m_transform = transform; }

  private:
    std::vector<Mesh> m_meshes;
    glm::mat4         m_transform = glm::mat4(1.0f);

    bool        m_isLoaded = false;
    std::string m_directory;
};
