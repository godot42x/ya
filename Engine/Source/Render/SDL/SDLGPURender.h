#pragma once

#include <optional>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include "Render/CommandBuffer.h"
#include "Render/Render.h"



#include "Core/FileSystem/FileSystem.h"
#include "Render/Shader.h"



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

    uint32_t getVertexBufferSize() const
    {
        return maxVertexBufferElemSize * vertexInputSize;
    }

    uint32_t getIndexBufferSize() const
    {
        return maxIndexBufferElemSize * sizeof(uint32_t);
    }

    bool init()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }

        int n = SDL_GetNumGPUDrivers();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d Available drivers: ", n);
        for (int i = 0; i < n; ++i) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetGPUDriver(i));
        }

        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                         SDL_GPU_SHADERFORMAT_DXIL |
                                         SDL_GPU_SHADERFORMAT_MSL,
                                     true,
                                     nullptr);
        if (!device) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create GPU device: %s", SDL_GetError());
            return false;
        }

        const char *driver = SDL_GetGPUDeviceDriver(device);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Choosen GPU Driver: %s", driver);

        window = SDL_CreateWindow("Neon", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
            return false;
        }

        createSamplers();
        return true;
    }

    void cleanContext()
    {
        for (auto &[key, sampler] : samplers) {
            if (!sampler) {
                continue;
            }
            SDL_ReleaseGPUSampler(device, sampler);
        }

        if (vertexBuffer) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
        }
        if (indexBuffer) {
            SDL_ReleaseGPUBuffer(device, indexBuffer);
        }
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }

        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
    }

    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info) override;

    std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current()) override;

    std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> createShaders(const ShaderCreateInfo &shaderCI);

    void fillDefaultIndices(std::shared_ptr<CommandBuffer> commandBuffer, EGraphicPipeLinePrimitiveType primitiveType);

  private:
    void createSamplers();
};
