#pragma once

#include <source_location>
#include <string_view>


#include "Texture.h"


struct CommandBuffer
{
    std::source_location location;

    CommandBuffer() = default;
    CommandBuffer(std::source_location &&loc)
        : location(std::move(loc))
    {
    }

    // disable copy
    // CommandBuffer(const CommandBuffer &)            = delete;
    // CommandBuffer &operator=(const CommandBuffer &) = delete;

    virtual ~CommandBuffer() {}

    virtual void *getCommandBufferPtr() = 0;
    virtual void  ensureSubmitted()     = 0;
    virtual bool  submit()              = 0;

    // Direct buffer operations
    virtual void uploadVertexBuffers(const void *vertexData, uint32_t vertexDataSize)       = 0;
    virtual void uploadIndexBuffers(const void *indexData, uint32_t indexDataSize)          = 0;
    virtual void uploadTexture(SDL_GPUTexture *texture, void *data, uint32_t w, uint32_t h) = 0;
    virtual void setVertexUniforms(uint32_t slot_index, void *data, uint32_t dataSize)      = 0;

    // Texture creation operations
    virtual std::shared_ptr<Texture> createTexture(std::string_view filepath)                                                               = 0;
    virtual std::shared_ptr<Texture> createTextureFromBuffer(const void *data, uint32_t width, uint32_t height, const char *name = nullptr) = 0;

    // Helper for uploading both vertex and index data in one call
    void uploadBuffers(const void *vertexData, uint32_t vertexDataSize, const void *indexData, uint32_t indexDataSize);
};
