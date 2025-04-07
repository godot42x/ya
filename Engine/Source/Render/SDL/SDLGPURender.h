#pragma once
#include "Render/Render.h"

#include <memory>
#include <optional>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>


#include "Render/CommandBuffer.h"


struct GPURender_SDL : public Render
{
    SDL_GPUDevice                                     *device;
    SDL_Window                                        *window;
    SDL_GPUGraphicsPipeline                           *pipeline;
    SDL_GPUBuffer                                     *vertexBuffer;
    SDL_GPUBuffer                                     *indexBuffer;
    std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;

    uint32_t maxVertexBufferElemSize = 10000;
    uint32_t maxIndexBufferElemSize  = 10000;
    uint32_t vertexInputSize         = -1;

    uint32_t getVertexBufferSize() const { return maxVertexBufferElemSize * vertexInputSize; }
    uint32_t getIndexBufferSize() const { return maxIndexBufferElemSize * sizeof(uint32_t); }

    bool init() override;
    void clean() override;
    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info) override;

    void *getNativeDevice()
    {
        return device;
    }
    void *getNativeWindow()
    {
        return window;
    }


    std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> createShaders(const ShaderCreateInfo &shaderCI);

    std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current()) override;

    void fillDefaultIndices(std::shared_ptr<CommandBuffer> commandBuffer, EGraphicPipeLinePrimitiveType primitiveType);

  private:
    void createSamplers();
};
