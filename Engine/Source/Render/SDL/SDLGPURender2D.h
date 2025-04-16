#pragma once

#include <array>
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

    struct VertexInput
    {
        glm::vec3 position;
        glm::vec4 color;
    };

    SDL_GPUDevice      *device = nullptr;
    SDLGraphicsPipeLine pipeline;

    std::vector<VertexInput> vertexInputBuffer;
    std::vector<Uint32>      indexInputBuffer;
    std::size_t              lastMaxIndexCapacity = 0;


    // Smart pointer buffer management
    SDLGPUBufferPtr         vertexBufferPtr         = nullptr;
    SDLGPUBufferPtr         indexBufferPtr          = nullptr;
    SDLGPUTransferBufferPtr vertexTransferBufferPtr = nullptr;

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
            window,
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


        std::size_t initialVertexCount = 1024 * 4; // 4 vertices per quad
        std::size_t initialIndexCount  = 1024 * 6; // 6 indices per quad

        std::size_t initialVertexBufferSize = initialVertexCount * sizeof(VertexInput);
        std::size_t initialIndexBufferSize  = initialIndexCount * sizeof(uint32_t);

        // Create initial buffers with default sizes using the new classes
        vertexBufferPtr = SDLGPUBuffer::Create(device, "Render2D VertexBuffer", SDLGPUBuffer::Usage::VertexBuffer, initialVertexBufferSize);
        indexBufferPtr  = SDLGPUBuffer::Create(device, "Render2D IndexBuffer", SDLGPUBuffer::Usage::IndexBuffer, initialIndexBufferSize);

        fillQuadIndicesToGPUBuffer(indexBufferPtr,
                                   initialIndexCount,
                                   initialIndexBufferSize);

        vertexTransferBufferPtr = SDLGPUTransferBuffer::Create(device, "Render2D VertexTransferBuffer", SDLGPUTransferBuffer::Usage::Upload, initialVertexBufferSize);

        vertexInputBuffer.reserve(initialVertexCount);
        indexInputBuffer.reserve(initialIndexCount);
    }

    void clean()
    {
        vertexBufferPtr.reset();
        indexBufferPtr.reset();
        vertexTransferBufferPtr.reset();

        textures.clear();
        pipeline.clean();
    }

    void beginFrame(SDL_GPUCommandBuffer *commandBuffer, const Camera &camera)
    {
        currentCommandBuffer            = commandBuffer;
        cameraData.viewProjectionMatrix = camera.getViewProjectionMatrix();

        vertexInputBuffer.resize(0);
        indexInputBuffer.resize(0);
    }

    void submit()
    {
        if (vertexInputBuffer.size() == 0) {
            return;
        }

        vertexBufferPtr->tryExtendSize(sizeof(VertexInput) * vertexInputBuffer.size());
        vertexTransferBufferPtr->tryExtendSize(sizeof(VertexInput) * vertexInputBuffer.size());

        // TODO: how to reduce the max size when not needed?
        std::size_t curIndexInputBufferCapacity = indexInputBuffer.capacity();
        if (lastMaxIndexCapacity < curIndexInputBufferCapacity) // this vector has been extended
        {
            static constexpr std::size_t elemSize = sizeof(indexInputBuffer[0]);
            // extend gpu buffer
            indexBufferPtr->tryExtendSize(elemSize * curIndexInputBufferCapacity);

            // recreate index buffer and quad indices, contain a copy pass
            fillQuadIndicesToGPUBuffer(indexBufferPtr,
                                       curIndexInputBufferCapacity,
                                       curIndexInputBufferCapacity * elemSize);

            lastMaxIndexCapacity = curIndexInputBufferCapacity;
        }


        // Map and copy data to transfer buffer
        void *ptr = SDL_MapGPUTransferBuffer(device, vertexTransferBufferPtr->getBuffer(), true);
        std::memcpy(ptr, vertexInputBuffer.data(), sizeof(VertexInput) * vertexInputBuffer.size());
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
            .size   = static_cast<Uint32>(sizeof(VertexInput) * vertexInputBuffer.size()),
        };

        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);
    }

    void render(SDL_GPURenderPass *renderpass)
    {
        // set the camera data in current pipeline(shader)
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
            indexInputBuffer.size(),
            1,
            0,
            0,
            0);
    }


    void drawQuad(const glm::vec2 &position, float rotation, const glm::vec2 &scale, const glm::vec4 &color)
    {
        static constexpr size_t numVertices = 4;

        glm::mat4 transform = glm::translate(glm::mat4(1.0), glm::vec3(position.x, position.y, 0.0)) *
                              glm::rotate(glm::mat4(1.0), glm::radians(rotation), glm::vec3(0, 0, 1)) *
                              glm::scale(glm::mat4(1.0), glm::vec3(scale.x, scale.y, 1.0));


        // Add the four vertices for this quad
        for (int i = 0; i < numVertices; ++i) {
            glm::vec4 transformedPos = transform * vertexPos[i];
            vertexInputBuffer.emplace_back(
                VertexInput{
                    .position = glm::vec3(transformedPos.x, transformedPos.y, transformedPos.z),
                    .color    = color,
                });
        }
    }


    void fillQuadIndicesToGPUBuffer(SDLGPUBufferPtr indexBuffer, std::size_t indicesSize, std::size_t bufferSize);
};

} // namespace SDL