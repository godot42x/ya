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
    SDL_GPUBuffer                      *vertexBuffer = nullptr;
    SDL_GPUTransferBuffer              *vertexTransferBuffer;
    SDL_GPUBuffer                      *indexBuffer;


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
        this->device = device;
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

        vertexBuffer = SDLBuffer::createBuffer(
            device,
            SDLBuffer::BufferCreateInfo{
                .name  = "VertexBuffer",
                .usage = SDLBuffer::BufferCreateInfo::VertexBuffer,
                .size  = 1024 * sizeof(VertexInput),
            });
        indexBuffer = SDLBuffer::createBuffer(
            device,
            SDLBuffer::BufferCreateInfo{
                .name  = "IndexBuffer",
                .usage = SDLBuffer::BufferCreateInfo::IndexBuffer,
                .size  = 1000 * 1024 * sizeof(uint32_t),
            });
        vertexTransferBuffer = SDLBuffer::createTransferBuffer(
            device,
            SDLBuffer::TransferBufferCreateInfo{
                .name  = "VertexTransferBuffer",
                .usage = SDLBuffer::TransferBufferCreateInfo::Upload,
                .size  = 1000 * 1024 * sizeof(VertexInput),
            });

        this->initQuadIndexBuffer();
    }

    void clean()
    {
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        SDL_ReleaseGPUBuffer(device, indexBuffer);
        SDL_ReleaseGPUTransferBuffer(device, vertexTransferBuffer);
        pipeline.clean();
        textures.clear();
    }

    void beginFrame(SDL_GPUCommandBuffer *commandBuffer, const Camera &camera)
    {
        currentCommandBuffer            = commandBuffer;
        cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();

        for (auto &buffer : vertexInputBuffer) {
            buffer.resize(0); // Reset size without deallocating memory
        }
    }

    void submit()
    {
        size_t totalSize = 0;
        for (const auto &buffer : vertexInputBuffer) {
            totalSize += buffer.size();
        }

        if (totalSize * sizeof(VertexInput) > SDLBuffer::getBufferSize(vertexBuffer)) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
            vertexBuffer = SDLBuffer::createBuffer(
                device,
                SDLBuffer::BufferCreateInfo{
                    .name  = "VertexBuffer",
                    .usage = SDLBuffer::BufferCreateInfo::VertexBuffer,
                    .size  = totalSize * sizeof(VertexInput),
                });
        }

        void  *ptr    = SDL_MapGPUTransferBuffer(device, vertexTransferBuffer, true);
        size_t offset = 0;
        for (const auto &buffer : vertexInputBuffer) {
            std::memcpy(static_cast<uint8_t *>(ptr) + offset, buffer.data(), buffer.size() * sizeof(VertexInput));
            offset += buffer.size() * sizeof(VertexInput);
        }
        SDL_UnmapGPUTransferBuffer(device, vertexTransferBuffer);

        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = vertexTransferBuffer,
            .offset          = 0,
        };
        SDL_GPUBufferRegion destination = {
            .buffer = vertexBuffer,
            .offset = 0,
        };

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(currentCommandBuffer);
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }

    void render(SDL_GPURenderPass *renderpass)
    {
        SDL_PushGPUVertexUniformData(
            currentCommandBuffer,
            0,
            &cameraData,
            sizeof(CameraData));

        SDL_BindGPUGraphicsPipeline(renderpass, pipeline.pipeline);

        SDL_GPUBufferBinding vertexBufferBinding = {
            .buffer = vertexBuffer,
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

        SDL_GPUBufferBinding indexBufferBinding = {
            .buffer = indexBuffer,
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(
            renderpass,
            vertexInputBuffer.size() * 6, // Assuming 6 indices per quad
            1,
            0,
            0,
            0);
    }

    void drawQuad(const glm::vec2 &position, float rotation, const glm::vec2 &scale, const glm::vec4 &color)
    {
        glm::mat4 transform = glm::scale(glm::mat4(1.0), glm::vec3(scale.x, scale.y, 1.0)) *
                              glm::rotate(glm::mat4(1.0), glm::radians(rotation), glm::vec3(0, 0, 1)) *
                              glm::translate(glm::mat4(1.0), glm::vec3(position.x, position.y, 0.0));

        if (currentBufferIndex >= vertexInputBuffer.size()) {
            vertexInputBuffer.emplace_back(); // Add a new buffer if needed
        }

        auto &currentBuffer = vertexInputBuffer[currentBufferIndex];
        for (const auto &pos : vertexPos) {
            if (currentBuffer.size() < maxVerticesPerBuffer) {
                currentBuffer.emplace_back(
                    VertexInput{
                        .position = transform * pos,
                        .color    = color,
                    });
            }
        }

        if (currentBuffer.size() >= maxVerticesPerBuffer) {
            currentBufferIndex++;
        }
    }


  private:
    // init the indexBuffer for each *QUAD* indices
    void initQuadIndexBuffer();
};

} // namespace SDL