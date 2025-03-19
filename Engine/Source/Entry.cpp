
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



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int n = SDL_GetNumGPUDrivers();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d Available drivers: ", n);
    for (int i = 0; i < n; ++i) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetGPUDriver(i));
    }

    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
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

    SDL_Window *window = SDL_CreateWindow("Neon", 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }


    SDL_GPUTextureFormat gpuTextureFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUColorTargetDescription colorTargetDesc{
        .format = gpuTextureFormat,
    };



    SDL_GPUGraphicsPipelineCreateInfo info{
        .vertex_input_state = SDL_GPUVertexInputState{
            .vertex_buffer_descriptions = 0,
            .num_vertex_buffers         = 0,
            .num_vertex_attributes      = 0,
        },
        .target_info = SDL_GPUGraphicsPipelineTargetInfo{
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = false,
        },
    };

    ShaderScriptProcessorFactory factory;
    factory.withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
        .withShaderStoragePath("Engine/Shader/GLSL")
        .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
        .syncCreateStorage(true);
    std::shared_ptr<ShaderScriptProcessor> processor = factory.FactoryNew();


    auto ret = processor->process("Test.glsl");
    if (!ret) {
        NE_CORE_ERROR("Failed to process shader: {}", processor->m_FilePath.string());
        NE_CORE_ASSERT(false);
    }

    ShaderScriptProcessor::stage2spirv_t &codes = ret.value();


    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);


    SDL_GPUShaderCreateInfo createInfo[] = {
        SDL_GPUShaderCreateInfo{
            .code_size            = codes[EShaderStage::Vertex].size(),
            .code                 = (uint8_t *)codes[EShaderStage::Vertex].data(),
            .entrypoint           = "vs_main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = 0,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 0,

        },
        SDL_GPUShaderCreateInfo{
            .code_size            = codes[EShaderStage::Fragment].size(),
            .code                 = (uint8_t *)codes[EShaderStage::Fragment].data(),
            .entrypoint           = "fs_main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = 0,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 0,
        }};

    SDL_GPUShader *vertexShader = SDL_CreateGPUShader(device, createInfo);

    // SDL_Storage *storage = openFileStorage("Engine/Content/Test/", true);
    // const char  *text    = "abc";
    // SDL_WriteStorageFile(storage, "abc", (void *)"abc", strlen(text));
    // SDL_CloseStorage(storage);



    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{

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
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sdl quit with result: %u", result);
}
