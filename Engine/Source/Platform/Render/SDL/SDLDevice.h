#pragma once

#include <memory>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "Render/CommandBuffer.h"

#include "Render/Device.h"

#include "reflect.cc/enum"

namespace SDL
{


class SDLBufferManager;

struct SDLDevice : LogicalDevice
{
    std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;

    SDL_Window * sdlWindow = nullptr;

    bool init(const InitParams &params) override;

    void createSamplers();

    void clean()
    {
        auto sdlDevice = getNativeDevicePtr<SDL_GPUDevice>();
        SDL_ReleaseWindowFromGPUDevice(sdlDevice, sdlWindow);
        SDL_DestroyGPUDevice(sdlDevice);
    }

    std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current());
};

} // namespace SDL