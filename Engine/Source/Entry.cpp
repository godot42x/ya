
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include "Core/FileSystem.h"

SDL_GPUGraphicsPipeline *pipeline;



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int n = SDL_GetNumGPUDrivers();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d Avaliable drivers: ", n);
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
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Choosed GPU Driver: %s", driver);

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
    // pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);



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
