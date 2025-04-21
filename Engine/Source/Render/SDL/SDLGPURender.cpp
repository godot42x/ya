#include "SDLGPURender.h"



#include "SDL3/SDL.h"
#include "SDL3/SDL_gpu.h"


#include "Render/Shader.h"
#include "SDLGPUCommandBuffer.h"

namespace SDL
{

std::shared_ptr<CommandBuffer> SDLRender3D::acquireCommandBuffer(std::source_location location)
{

    // return std::shared_ptr<CommandBuffer>(new GPUCommandBuffer_SDL(this, std::move(location)));
    return std::make_shared<GPUCommandBuffer_SDL>(this, std::move(location));
    // return nullptr;
}


bool SDLRender3D::init(bool bVsync)
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
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Chosen GPU Driver: %s", driver);

    window = SDL_CreateWindow("Neon", 1024, 768, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
        return false;
    }

    SDL_SetGPUSwapchainParameters(device,
                                  window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  bVsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE);

    return true;
}

void SDLRender3D::clean()
{
    if (vertexBuffer) {
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
    }
    if (indexBuffer) {
        SDL_ReleaseGPUBuffer(device, indexBuffer);
    }
    _pipeline.clean();

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
}

// a Pipeline: 1 vertex shader + 1 fragment shader + 1 render pass + 1 vertex buffer + 1 index buffer
// their format should be compatible with each other
// so we put them together with initialization
bool SDLRender3D::createGraphicsPipeline(const GraphicsPipelineCreateInfo &pipelineCI)
{

    _pipeline.create(device, window, pipelineCI);

    // create global big size buffer for batch draw call
    {
        {
            SDL_GPUBufferCreateInfo bufferInfo = {
                .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                .size  = getVertexBufferSize(),
                .props = 0, // by comment
            };

            vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
            NE_ASSERT(vertexBuffer, "Failed to create vertex buffer {}", SDL_GetError());

            SDL_SetGPUBufferName(device, vertexBuffer, "godot42 vertex buffer 😍");
        }

        {
            SDL_GPUBufferCreateInfo bufferInfo = {
                .usage = SDL_GPU_BUFFERUSAGE_INDEX,
                .size  = getIndexBufferSize(),
                .props = 0, // by comment
            };
            indexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
            NE_ASSERT(indexBuffer, "Failed to create index buffer {}", SDL_GetError());

            SDL_SetGPUBufferName(device, indexBuffer, "godot42 index buffer 😁");
        }

        // unifrom created and specify by shader create info
    }


    return _pipeline.pipeline != nullptr;
}



} // namespace SDL