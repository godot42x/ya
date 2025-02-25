
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>



SDLMAIN_DECLSPEC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int n = SDL_GetNumGPUDrivers();
    for (int i = 0; i < n; ++i) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPU driver: %s", SDL_GetGPUDriver(i));
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

    SDL_Window *window = SDL_CreateWindow("Neon", 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }



    return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "event: %u", event->type);

    switch ((SDL_EventType)event->type) {
    case SDL_EventType::SDL_EVENT_KEY_UP:
    {
        if (event->key.key == SDLK_Q) {
            return SDL_APP_SUCCESS;
        }
        break;
    }
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "result: %u", result);
}
