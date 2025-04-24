#pragma once

#include "Render/CommandBuffer.h"

#include "Core/Log.h"
#include "SDL3/SDL_gpu.h"
#include "SDLDevice.h"


struct SDL_GPUTexture;

namespace SDL
{

struct SDLDevice;

struct SDLGPUCommandBuffer : public CommandBuffer
{

    // Constructor now uses SDLDevice instead of SDLRender3D
    SDLGPUCommandBuffer(SDLDevice &device, std::source_location &&loc)
        : CommandBuffer(device, std::move(loc))
    {
        auto sdlDevice = device.getNativeDevicePtr<SDL_GPUDevice>();

        SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(sdlDevice);
        NE_ASSERT(commandBuffer, "Failed to create command buffer {}", SDL_GetError());
        nativeCommandBuffer = commandBuffer;
    }

    ~SDLGPUCommandBuffer() override
    {
        ensureSubmitted();
    }

    bool submit() override
    {
        auto sdlCommandBuffer = getNativeCommandBufferPtr<SDL_GPUCommandBuffer>();
        NE_CORE_ASSERT(sdlCommandBuffer != nullptr,
                       "commandBuffer is already submitted! buffer acquired at {}:{}",
                       location.file_name(),
                       location.line());

        if (!SDL_SubmitGPUCommandBuffer(sdlCommandBuffer)) {
            NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
            return false;
        }
        nativeCommandBuffer = nullptr; // reset command buffer to null, so we can acquire a new one
        bSubmitted          = true;
        return true;
    }
};

} // namespace SDL
