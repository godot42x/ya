#pragma once

#include <optional>
#include <vector>


#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>



#include "Core/FileSystem/FileSystem.h"
#include "Render/Shader.h"


#include "reflect.cc/enum.h"



enum class ESamplerType
{
    DefaultLinear = 0,
    DefaultNearest,
    PointClamp,
    PointWrap,
    LinearClamp,
    LinearWrap,
    AnisotropicClamp,
    AnisotropicWrap,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(ESamplerType);



struct VertexBufferDescription
{
    uint32_t slot;
    uint32_t pitch;
};


enum EVertexAttributeFormat
{
    Float2 = 0,
    Float3,
    Float4,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(EVertexAttributeFormat);


struct VertexAttribute
{

    uint32_t               location;
    uint32_t               bufferSlot;
    EVertexAttributeFormat format;
    uint32_t               offset;
};


struct GraphicsPipelineCreateInfo
{
    std::string                          shaderName; // we use single glsl now
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
};

struct Render
{
};



struct RenderAPI
{
    enum EAPIType
    {
        None = 0,
        OpenGL,
        Vulkan,
        DirectX12,
        Metal,
        SDL3GPU, // SDL3 GPU backend
        ENUM_MAX,
    };
};

struct SDLGPURender : public Render
{
    SDL_GPUDevice                                     *device;
    SDL_Window                                        *window;
    SDL_GPUGraphicsPipeline                           *pipeline;
    SDL_GPUBuffer                                     *vertexBuffer;
    SDL_GPUBuffer                                     *indexBuffer;
    std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;

    uint32_t vertexBufferSize = 10000;
    uint32_t indexBufferSize  = 10000;

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

        // swapchain just be created here, change its parameter after creation
        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
            return false;
        }


        // default is vsync, we can change it to immediate or mailbox
        // SDL_SetGPUSwapchainParameters(device,
        //                               window,
        //                               SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        //                               SDL_GPU_PRESENTMODE_IMMEDIATE);

        createSamplers();
        return true;
    }


    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info);

    void cleanContext()
    {
        // clear samplers
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


    std::optional<std::tuple<SDL_GPUShader *, SDL_GPUShader *>> createShaders(std::string_view shaderName);
    SDL_GPUTexture                                             *createTexture(std::string_view filepath);


    void uploadBuffers(SDL_GPUCommandBuffer *commandBuffer, void *vertexData, Uint32 inputVerticesSize, void *indexData, Uint32 inputIndicesSize);


  private:
    void createSamplers();
};
