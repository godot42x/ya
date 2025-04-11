#pragma once

#include "Core/Camera.h"
#include "Render/Texture.h"
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <vector>

struct Render2D
{
    // 2D render
    SDL_GPUGraphicsPipeline *pipeline;
    uint32_t                 maxBatchVertexBufferElemSize = 1000 * 1024;
    uint32_t                 maxBatchIndexBufferElemSize  = 1000 * 1024;


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