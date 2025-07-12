#pragma once

#include <source_location>
#include <string_view>

#include "Core/Log.h"
// #include "Texture.h"

#include "Device.h"

struct CommandBuffer
{
    LogicalDevice       &device;
    std::source_location location;
    bool                 bSubmitted = false;

    void *nativeCommandBuffer = nullptr;

    CommandBuffer(LogicalDevice &device, std::source_location &&loc)
        : device(device), location(std::move(loc))
    {
    }

    // disable copy
    // CommandBuffer(const CommandBuffer &)            = delete;
    // CommandBuffer &operator=(const CommandBuffer &) = delete;

    virtual ~CommandBuffer()
    {
        ensureSubmitted();
    }

    virtual bool submit() = 0;

    void ensureSubmitted()
    {
        NE_CORE_ASSERT(bSubmitted,
                       "command buffer should be submitted manually before destruction! buffer acquired at {}:{}",
                       location.file_name(),
                       location.line());
    }

    template <class NativeType>
    NativeType *getNativeCommandBufferPtr()
    {
        return static_cast<NativeType *>(nativeCommandBuffer);
    }
};
