#pragma once

#include <glm/glm.hpp>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Core/Camera.h"
#include "Render/SDL/SDLGraphicsPipeline.h"
#include "Render/Texture.h"



namespace SDL
{

struct SDLRender2D
{
    SDL_GPUDevice *device = nullptr;

    SDLGraphicsPipeLine pipeline;

    uint32_t maxBatchVertexBufferElemSize = 1000 * 1024;
    uint32_t maxBatchIndexBufferElemSize  = 1000 * 1024;


    SDL_GPUBuffer         *vertexBuffer;
    SDL_GPUBuffer         *indexBuffer;
    SDL_GPUTransferBuffer *vertexTransferBuffer;

    struct VertexInput
    {
        glm::vec2 position;
        glm::vec4 color;
        glm::vec2 uv; // aka texcoord
    };

    std::vector<std::shared_ptr<Texture>> textures;
    std::shared_ptr<Texture>              whiteTexture;

    uint32_t vertexCount = 0;

    void init()
    {
    }

    void clean()
    {
    }

    void begin(const Camera &camera)
    {
        vertexCount = 0;
    }
};

} // namespace SDL