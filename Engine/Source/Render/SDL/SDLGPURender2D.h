#pragma once

#include <array>
#include <vector>



#include <glm/glm.hpp>

#include <SDL3/SDL_gpu.h>

#include "Core/Camera.h"
#include "Render/SDL/SDLBuffers.h"
#include "Render/SDL/SDLGraphicsPipeline.h"
#include "Render/Texture.h"
#include "glm/ext/matrix_transform.hpp"



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


    std::vector<std::shared_ptr<Texture>> textures;
    std::shared_ptr<Texture>              whiteTexture;

    uint32_t vertexCount = 0;

    std::array<glm::vec4, 4> vertexPos = {
        glm::vec4(-0.5f, 0.5f, 0.0, 1.0),  // left top
        glm::vec4(0.5f, 0.5f, 0.0, 1.0),   // right top
        glm::vec4(0.5f, -0.5f, 0.0, 0.0),  // right bottom
        glm::vec4(-0.5f, -0.5f, 0.0, 0.0), // left bottom
    };
    struct VertexInput
    {
        glm::vec3 position;
        glm::vec4 color;
        glm::vec2 uv; // aka texcoord
    };

    struct CameraData
    {
        glm::mat4 viewProjectionMatrix;
    } cameraData;

    VertexInput *vertexInputHead = nullptr;
    VertexInput *vertexInputPtr  = nullptr;


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
                    VertexAttribute{
                        .location   = 2,
                        .bufferSlot = 0,
                        .format     = EVertexAttributeFormat::Float2,
                        .offset     = offsetof(VertexInput, uv),
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
                .size  = maxBatchVertexBufferElemSize * sizeof(VertexInput),
            });
        indexBuffer = SDLBuffer::createBuffer(
            device,
            SDLBuffer::BufferCreateInfo{
                .name  = "IndexBuffer",
                .usage = SDLBuffer::BufferCreateInfo::IndexBuffer,
                .size  = maxBatchIndexBufferElemSize * sizeof(uint32_t),
            });
        vertexTransferBuffer = SDLBuffer::createTransferBuffer(
            device,
            SDLBuffer::TransferBufferCreateInfo{
                .name  = "VertexTransferBuffer",
                .usage = SDLBuffer::TransferBufferCreateInfo::Upload,
                .size  = maxBatchVertexBufferElemSize * sizeof(VertexInput),
            });


        this->initIndexBuffer();
    }

    void clean()
    {
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        SDL_ReleaseGPUBuffer(device, indexBuffer);
        SDL_ReleaseGPUTransferBuffer(device, vertexTransferBuffer);
        pipeline.clean();
        textures.clear();
    }

    void begin(SDL_GPUCommandBuffer *commandBuffer, const Camera &camera)
    {
        currentCommandBuffer = commandBuffer;

        vertexInputHead = (VertexInput *)SDL_MapGPUTransferBuffer(device, vertexTransferBuffer, vertexBuffer);
        vertexInputPtr  = vertexInputHead;
        vertexCount     = 0;

        cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();
    }

    void flush()
    {
        SDL_UnmapGPUTransferBuffer(device, vertexTransferBuffer);


        // upload
        auto                         *copyPass = SDL_BeginGPUCopyPass(currentCommandBuffer);
        SDL_GPUTransferBufferLocation source{
            .transfer_buffer = vertexTransferBuffer,
            .offset          = 0,
        };

        SDL_GPUBufferRegion destination{
            .buffer = vertexBuffer,
            .offset = 0,
        };
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);


        vertexInputPtr = vertexInputHead;
        vertexCount    = 0;
    }

    void end()
    {

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(currentCommandBuffer);

        static SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = vertexTransferBuffer,
            .offset          = 0,
        };
        static SDL_GPUBufferRegion destination = {
            .buffer = vertexBuffer,
            .offset = 0,
        };

        source.transfer_buffer = vertexTransferBuffer;
        destination.buffer     = vertexBuffer;

        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }

    void drawQuad(const glm::vec2 &position, float rotation, const glm::vec2 &scale, const glm::vec4 &color, const std::shared_ptr<Texture> &texture, const glm::vec2 &uv)
    {
        if (vertexCount + 4 >= maxBatchVertexBufferElemSize) {
            flush();
        }

        // left top

        // postion:  model pos -> translate -> rotate -> scale
        glm::mat4 transform = glm::scale(glm::mat4(1.0), glm::vec3(scale.x, scale.y, 1.0)) *
                              glm::rotate(glm::mat4(1.0), glm::radians(rotation), glm::vec3(0, 0, 1)) *
                              glm::translate(glm::mat4(1.0), glm::vec3(position.x, position.y, 0.0));

        for (int i = 0; i < 4; ++i) {
            vertexInputPtr->position = transform * vertexPos[0];
            vertexInputPtr->color    = color;
            vertexInputPtr->uv       = uv;
            ++vertexInputPtr;
        }

        vertexCount += 4;
    }


  private:
    // init the indexBuffer for each *QUAD* indices
    void initIndexBuffer()
    {

        SDL_GPUTransferBuffer *indexTransferBuffer = SDLBuffer::createTransferBuffer(
            device,
            SDLBuffer::TransferBufferCreateInfo{
                .name  = "IndexTransferBuffer",
                .usage = SDLBuffer::TransferBufferCreateInfo::Upload,
                .size  = maxBatchIndexBufferElemSize * sizeof(uint32_t),
            });
        Uint32 *indicesPtr = (Uint32 *)SDL_MapGPUTransferBuffer(device, indexTransferBuffer, indexBuffer);

        if (pipeline.pipelineCreateInfo.frontFaceType == EFrontFaceType::ClockWise) {
            for (uint32_t i = 0; i < maxBatchIndexBufferElemSize / 6; i++) {
                indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
                indicesPtr[i * 6 + 1] = i * 4 + 1; // right top
                indicesPtr[i * 6 + 2] = i * 4 + 2; // right bottom

                indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
                indicesPtr[i * 6 + 4] = i * 4 + 2; // right bottom
                indicesPtr[i * 6 + 5] = i * 4 + 3; // left bottom
            }
        }
        else {
            for (uint32_t i = 0; i < maxBatchIndexBufferElemSize / 6; i++) {
                indicesPtr[i * 6 + 0] = i * 4 + 0; // left top
                indicesPtr[i * 6 + 1] = i * 4 + 2; // right bottom
                indicesPtr[i * 6 + 2] = i * 4 + 1; // right top

                indicesPtr[i * 6 + 3] = i * 4 + 0; // left top
                indicesPtr[i * 6 + 4] = i * 4 + 3; // left bottom
                indicesPtr[i * 6 + 5] = i * 4 + 2; // right bottom
            }
        }

        SDL_UnmapGPUTransferBuffer(device, indexTransferBuffer);

        auto commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        auto copyPass      = SDL_BeginGPUCopyPass(commandBuffer);
        {
            SDL_GPUTransferBufferLocation source = {
                .transfer_buffer = indexTransferBuffer,
                .offset          = 0,
            };
            SDL_GPUBufferRegion destination = {
                .buffer = indexBuffer,
                .offset = 0,
            };
            SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
        }
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device, indexTransferBuffer);
    }
};

} // namespace SDL