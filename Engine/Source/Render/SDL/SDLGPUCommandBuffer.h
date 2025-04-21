#pragma once

#include "Render/CommandBuffer.h"

#include "Core/Log.h"
#include "SDL3/SDL_gpu.h"

struct SDL_GPUTexture;

namespace SDL {

struct SDLRender3D;

struct GPUCommandBuffer_SDL : public CommandBuffer
{
    SDLRender3D        *r             = nullptr;
    SDL_GPUCommandBuffer *commandBuffer = nullptr;

    GPUCommandBuffer_SDL(SDLRender3D *render, std::source_location &&loc);

    ~GPUCommandBuffer_SDL() override
    {
        ensureSubmitted();
    }

    void ensureSubmitted() override
    {
        if (NE_ENSURE(commandBuffer == nullptr,
                      "command buffer should be submitted manually before destruction! buffer acquired at {}:{}",
                      location.file_name(),
                      location.line()))
        {
            SDL_SubmitGPUCommandBuffer(commandBuffer);
            commandBuffer = nullptr;
        }
    }

    bool submit() override
    {
        NE_CORE_ASSERT(commandBuffer != nullptr,
                       "commandBuffer is already submitted! buffer acquired at {}:{}",
                       location.file_name(),
                       location.line());

        if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
            NE_CORE_ERROR("Failed to submit command buffer {}", SDL_GetError());
            return false;
        }
        commandBuffer = nullptr; // reset command buffer to null, so we can acquire a new one
        return true;
    }

    void *getCommandBufferPtr() override { return commandBuffer; }

    void uploadVertexBuffers(const void *vertexData, uint32_t vertexDataSize) override;
    void uploadIndexBuffers(const void *indexData, uint32_t indexDataSize) override;
    void uploadTexture(SDL_GPUTexture *texture, void *data, uint32_t w, uint32_t h) override;
    void setVertexUniforms(uint32_t slot_index, void *data, uint32_t dataSize) override;
    void setFragmentUniforms(uint32_t slot_index, void *data, uint32_t dataSize) override;


    std::shared_ptr<Texture> createTexture(std::string_view filepath) override;
    std::shared_ptr<Texture> createTextureFromBuffer(const void *data, Uint32 width, Uint32 height, const char *name) override;
};

} // namespace SDL
