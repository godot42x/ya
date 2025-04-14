#pragma once

#include <array>
#include <list>
#include <vector>


#include <glm/glm.hpp>

#include <SDL3/SDL_gpu.h>

#include "Core/Camera.h"
#include "Render/SDL/SDLBuffers.h"
#include "Render/SDL/SDLGraphicsPipeline.h"
#include "glm/ext/matrix_transform.hpp"



namespace SDL
{

struct SDLRender2D
{
    SDL_GPUDevice *device = nullptr;

    SDLGraphicsPipeLine pipeline;

    struct VertexInput
    {
        glm::vec3 position;
        glm::vec4 color;
    };
    std::list<std::vector<VertexInput>> vertexInputBuffer;

    // Smart pointer buffer management
    SDLGPUBufferPtr         vertexBufferPtr         = nullptr;
    SDLGPUBufferPtr         indexBufferPtr          = nullptr;
    SDLGPUTransferBufferPtr vertexTransferBufferPtr = nullptr;

    // Constants for buffer management
    static constexpr uint32_t maxVerticesPerBuffer        = 1000;
    static constexpr uint32_t maxBatchIndexBufferElemSize = 6000; // Support for 1000 quads (6 indices per quad)
    static constexpr size_t   initialVertexBufferSize     = 1024 * sizeof(VertexInput);
    static constexpr size_t   initialIndexBufferSize      = 6000 * sizeof(uint32_t);

    uint32_t currentBufferIndex = 0;

    std::vector<std::shared_ptr<Texture>> textures;
    std::shared_ptr<Texture>              whiteTexture;


    std::array<glm::vec4, 4> vertexPos = {
        glm::vec4(-0.5f, 0.5f, 0.0, 1.0),  // left top
        glm::vec4(0.5f, 0.5f, 0.0, 1.0),   // right top
        glm::vec4(0.5f, -0.5f, 0.0, 0.0),  // right bottom
        glm::vec4(-0.5f, -0.5f, 0.0, 0.0), // left bottom
    };

    struct CameraData
    {
        glm::mat4 viewProjectionMatrix;
    } cameraData;


    SDL_GPUCommandBuffer *currentCommandBuffer = nullptr;

    void init(SDL_GPUDevice *device, SDL_Window *window)
    {
        this->device       = device;
        currentBufferIndex = 0;

        pipeline.create(
            device,
            nullptr,
            GraphicsPipelineCreateInfo{
                .bDeriveInfoFromShader = false,
                .shaderCreateInfo      = ShaderCreateInfo{
                         .shaderName = "Sprite2D.glsl",
                },
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot  = 0,
                        .pitch = sizeof(VertexInput),
                    },
                },
                .vertexAttributes = {
                    VertexAttribute{
                        .location   = 0,
                        .bufferSlot = 0,
                        .format     = EVertexAttributeFormat::Float2,
                        .offset     = offsetof(VertexInput, position),
                    },
                    VertexAttribute{
                        .location   = 1,
                        .bufferSlot = 0,
                        .format     = EVertexAttributeFormat::Float4,
                        .offset     = offsetof(VertexInput, color),
                    },
                },
                .primitiveType = EGraphicPipeLinePrimitiveType::TriangleList,
                .frontFaceType = EFrontFaceType::CounterClockWise,
            });

        // Create initial buffers with default sizes using the new classes
        vertexBufferPtr         = SDLGPUBuffer::Create(*device, "Render2D VertexBuffer", SDLGPUBuffer::Usage::VertexBuffer, initialVertexBufferSize);
        indexBufferPtr          = SDLGPUBuffer::Create(*device, "Render2D IndexBuffer", SDLGPUBuffer::Usage::IndexBuffer, initialIndexBufferSize);
        vertexTransferBufferPtr = SDLGPUTransferBuffer::Create(*device, "Render2D VertexTransferBuffer", SDLGPUTransferBuffer::Usage::Upload, initialVertexBufferSize);

        // Create the first buffer in the list
        vertexInputBuffer.emplace_back();
        vertexInputBuffer.back().reserve(maxVerticesPerBuffer);

        this->initQuadIndexBuffer();
    }

    void clean()
    {
        // Clear smart pointers which will automatically release resources
        vertexBufferPtr.reset();
        indexBufferPtr.reset();
        vertexTransferBufferPtr.reset();

        pipeline.clean();
        textures.clear();
    }

    void beginFrame(SDL_GPUCommandBuffer *commandBuffer, const Camera &camera)
    {
        currentCommandBuffer            = commandBuffer;
        cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();

        // Reset all buffers
        for (auto &buffer : vertexInputBuffer) {
            buffer.resize(0); // Reset size without deallocating memory
        }
        currentBufferIndex = 0;
    }

    void submit()
    {
        size_t totalVertices = 0;
        for (const auto &buffer : vertexInputBuffer) {
            totalVertices += buffer.size();
        }

        if (totalVertices == 0) {
            return; // Nothing to render
        }

        size_t requiredVertexBufferSize = totalVertices * sizeof(VertexInput);

        // Expand vertex buffer if needed using tryExtendSize
        if (!vertexBufferPtr) {
            vertexBufferPtr = SDLGPUBuffer::Create(*device, "VertexBuffer", SDLGPUBuffer::Usage::VertexBuffer, requiredVertexBufferSize);
        }
        else {
            vertexBufferPtr->tryExtendSize("VertexBuffer", SDLGPUBuffer::Usage::VertexBuffer, requiredVertexBufferSize);
        }

        // Expand transfer buffer if needed
        if (!vertexTransferBufferPtr) {
            vertexTransferBufferPtr = SDLGPUTransferBuffer::Create(*device, "VertexTransferBuffer", SDLGPUTransferBuffer::Usage::Upload, requiredVertexBufferSize);
        }
        else {
            vertexTransferBufferPtr->tryExtendSize("VertexTransferBuffer",
                                                   SDLGPUTransferBuffer::Usage::Upload,
                                                   requiredVertexBufferSize);
        }

        // Map and copy data to transfer buffer
        void  *ptr    = SDL_MapGPUTransferBuffer(device, vertexTransferBufferPtr->getBuffer(), true);
        size_t offset = 0;
        for (const auto &buffer : vertexInputBuffer) {
            if (buffer.size() > 0) {
                std::memcpy(static_cast<uint8_t *>(ptr) + offset, buffer.data(), buffer.size() * sizeof(VertexInput));
                offset += buffer.size() * sizeof(VertexInput);
            }
        }
        SDL_UnmapGPUTransferBuffer(device, vertexTransferBufferPtr->getBuffer());

        // Upload to GPU buffer
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(currentCommandBuffer);

        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = vertexTransferBufferPtr->getBuffer(),
            .offset          = 0,
        };

        SDL_GPUBufferRegion destination = {
            .buffer = vertexBufferPtr->getBuffer(),
            .offset = 0,
        };

        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);
    }

    void render(SDL_GPURenderPass *renderpass)
    {
        // Calculate total number of vertices to render
        size_t totalVertices = 0;
        for (const auto &buffer : vertexInputBuffer) {
            totalVertices += buffer.size();
        }

        if (totalVertices == 0) {
            return; // Nothing to render
        }

        // Calculate total number of quads and indices
        size_t totalQuads   = totalVertices / 4;
        size_t totalIndices = totalQuads * 6;

        SDL_PushGPUVertexUniformData(
            currentCommandBuffer,
            0,
            &cameraData,
            sizeof(CameraData));

        SDL_BindGPUGraphicsPipeline(renderpass, pipeline.pipeline);

        SDL_GPUBufferBinding vertexBufferBinding = {
            .buffer = vertexBufferPtr->getBuffer(),
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

        SDL_GPUBufferBinding indexBufferBinding = {
            .buffer = indexBufferPtr->getBuffer(),
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(
            renderpass,
            totalIndices, // Use actual number of indices
            1,
            0,
            0,
            0);
    }

    void drawQuad(const glm::vec2 &position, float rotation, const glm::vec2 &scale, const glm::vec4 &color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0), glm::vec3(position.x, position.y, 0.0)) *
                              glm::rotate(glm::mat4(1.0), glm::radians(rotation), glm::vec3(0, 0, 1)) *
                              glm::scale(glm::mat4(1.0), glm::vec3(scale.x, scale.y, 1.0));

        // Check if we need a new buffer
        if (currentBufferIndex >= vertexInputBuffer.size()) {
            vertexInputBuffer.emplace_back();
            vertexInputBuffer.back().reserve(maxVerticesPerBuffer);
        }

        auto &currentBuffer = vertexInputBuffer[currentBufferIndex];

        // Check if we need to move to the next buffer
        if (currentBuffer.size() + 4 > maxVerticesPerBuffer) {
            currentBufferIndex++;

            // Create a new buffer if needed
            if (currentBufferIndex >= vertexInputBuffer.size()) {
                vertexInputBuffer.emplace_back();
                vertexInputBuffer.back().reserve(maxVerticesPerBuffer);
            }

            currentBuffer = vertexInputBuffer[currentBufferIndex];
        }

        // Add the four vertices for this quad
        for (const auto &pos : vertexPos) {
            glm::vec4 transformedPos = transform * pos;
            currentBuffer.emplace_back(
                VertexInput{
                    .position = glm::vec3(transformedPos.x, transformedPos.y, transformedPos.z),
                    .color    = color,
                });
        }
    }


  private:
    // init the indexBuffer for each *QUAD* indices
    void initQuadIndexBuffer();
};

} // namespace SDL