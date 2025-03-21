
#include <cmath>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>

#include "Core/FileSystem.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>



#include "Render/Shader.h"

SDL_GPUGraphicsPipeline *pipeline;
SDL_GPUDevice           *device;
SDL_Window              *window;



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    Logger::init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
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
        return SDL_APP_FAILURE;
    }

    const char *driver = SDL_GetGPUDeviceDriver(device);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Choosen GPU Driver: %s", driver);

    window = SDL_CreateWindow("Neon", 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }



    ShaderScriptProcessorFactory factory;
    factory.withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
        .withShaderStoragePath("Engine/Shader/GLSL")
        .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
        .syncCreateStorage(true);
    std::shared_ptr<ShaderScriptProcessor> processor = factory.FactoryNew();


    auto ret = processor->process("Test.glsl");
    if (!ret) {
        NE_CORE_ERROR("Failed to process shader: {}", processor->tempProcessingPath);
        NE_CORE_ASSERT(false, "Failed to process shader: {}", processor->tempProcessingPath);
    }

    ShaderScriptProcessor::stage2spirv_t &codes = ret.value();



    SDL_GPUShaderCreateInfo vertexCrateInfo = {
        .code_size            = codes[EShaderStage::Vertex].size() * sizeof(uint32_t) / sizeof(uint8_t),
        .code                 = (uint8_t *)codes[EShaderStage::Vertex].data(),
        .entrypoint           = "main",
        .format               = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers         = 0,
        .num_storage_textures = 0,
        .num_storage_buffers  = 0,
        .num_uniform_buffers  = 0,

    };
    SDL_GPUShaderCreateInfo fragmentCreateInfo = {
        .code_size            = codes[EShaderStage::Fragment].size() * sizeof(uint32_t) / sizeof(uint8_t),
        .code                 = (uint8_t *)codes[EShaderStage::Fragment].data(),
        .entrypoint           = "main",
        .format               = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers         = 0,
        .num_storage_textures = 0,
        .num_storage_buffers  = 0,
        .num_uniform_buffers  = 0,
    };

    SDL_GPUShader *vertexShader = SDL_CreateGPUShader(device, &vertexCrateInfo);
    if (!vertexShader) {
        NE_CORE_ERROR("Failed to create vertex shader");
        return SDL_APP_FAILURE;
    }
    SDL_GPUShader *fragmentShader = SDL_CreateGPUShader(device, &fragmentCreateInfo);
    if (!fragmentShader) {
        NE_CORE_ERROR("Failed to create fragment shader");
        return SDL_APP_FAILURE;
    }

    // SDL_Storage *storage = openFileStorage("Engine/Content/Test/", true);
    // const char  *text    = "abc";
    // SDL_WriteStorageFile(storage, "abc", (void *)"abc", strlen(text));
    // SDL_CloseStorage(storage);

    SDL_GPUTextureFormat gpuTextureFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUColorTargetDescription colorTargetDesc{
        .format = gpuTextureFormat,
    };


    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader      = vertexShader,
        .fragment_shader    = fragmentShader,
        .vertex_input_state = SDL_GPUVertexInputState{
            .vertex_buffer_descriptions = 0,
            .num_vertex_buffers         = 0,
            .num_vertex_attributes      = 0,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info    = SDL_GPUGraphicsPipelineTargetInfo{
               .color_target_descriptions = &colorTargetDesc,
               .num_color_targets         = 1,
               .has_depth_stencil_target  = false,
        },
    };

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    return SDL_APP_CONTINUE;

    SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer) {
        NE_CORE_ERROR("Failed to acquire command buffer {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    Uint32          w, h;
    SDL_GPUTexture *swapchainTexture = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer,
                                               window,
                                               &swapchainTexture,
                                               &w,
                                               &h)) {
        NE_CORE_ERROR("Failed to acquire swapchain texture {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!swapchainTexture) {
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    // NE_CORE_TRACE("Event: {}", event->type);
    switch ((SDL_EventType)event->type) {
    case SDL_EventType::SDL_EVENT_KEY_UP:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Key up: %d", event->key.key);
        if (event->key.key == SDLK_Q)
        {
            return SDL_APP_SUCCESS;
        }
        break;
    }
    case SDL_EventType::SDL_EVENT_QUIT:
    {
        NE_CORE_INFO("SDL Quit");
        return SDL_APP_SUCCESS;
    }
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sdl quit with result: %u", result);
}
