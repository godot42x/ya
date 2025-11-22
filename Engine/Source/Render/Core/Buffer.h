// Buffer.h - Generic buffer interface for multi-backend rendering
#pragma once

#include "Core/Base.h"
#include "DescriptorSet.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ya
{

class IRender;

// Buffer usage flags (backend-agnostic)
enum class EBufferUsage : uint32_t
{
    None               = 0,
    TransferSrc        = 1 << 0,
    TransferDst        = 1 << 1,
    UniformTexelBuffer = 1 << 2,
    StorageTexelBuffer = 1 << 3,
    UniformBuffer      = 1 << 4,
    StorageBuffer      = 1 << 5,
    IndexBuffer        = 1 << 6,
    VertexBuffer       = 1 << 7,
    IndirectBuffer     = 1 << 8,
};

inline EBufferUsage operator|(EBufferUsage a, EBufferUsage b)
{
    return static_cast<EBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline EBufferUsage operator&(EBufferUsage a, EBufferUsage b)
{
    return static_cast<EBufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// Memory property flags (backend-agnostic)
enum class EMemoryProperty : uint32_t
{
    None            = 0,
    DeviceLocal     = 1 << 0, // GPU memory
    HostVisible     = 1 << 1, // CPU can access
    HostCoherent    = 1 << 2, // No need to flush/invalidate
    HostCached      = 1 << 3, // CPU cached
    LazilyAllocated = 1 << 4,
};

inline EMemoryProperty operator|(EMemoryProperty a, EMemoryProperty b)
{
    return static_cast<EMemoryProperty>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline EMemoryProperty operator&(EMemoryProperty a, EMemoryProperty b)
{
    return static_cast<EMemoryProperty>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct BufferCreateInfo
{
    std::string           label;
    EBufferUsage          usage;
    std::optional<void *> data = std::nullopt;
    uint32_t              size;
    EMemoryProperty       memProperties;
};

// Generic buffer interface
class IBuffer
{
  public:
    virtual ~IBuffer() = default;

    // Disable copy
    IBuffer(const IBuffer &)            = delete;
    IBuffer &operator=(const IBuffer &) = delete;

    // Enable move
    IBuffer(IBuffer &&)            = default;
    IBuffer &operator=(IBuffer &&) = default;

    // Write data to buffer
    virtual bool writeData(const void *data, uint32_t size = 0, uint32_t offset = 0) = 0;

    // Flush memory (for non-coherent memory)
    virtual bool flush(uint32_t size = 0, uint32_t offset = 0) = 0;

    // Map buffer memory
    template <typename T>
    T *map()
    {
        void *data = nullptr;
        mapInternal(&data);
        return static_cast<T *>(data);
    }

    // Unmap buffer memory
    virtual void unmap() = 0;

    // Get native handle (backend-specific)
    virtual BufferHandle getHandle() const = 0;

    // Get typed native handle
    template <typename T>
    T getHandleAs() const
    {
        return getHandle().as<T>();
    }

    // Get buffer size
    virtual uint32_t getSize() const = 0;

    // Check if buffer is host visible
    virtual bool isHostVisible() const = 0;

    // Get buffer name/label
    virtual const std::string &getName() const = 0;

    // Factory method
    static std::shared_ptr<IBuffer> create(IRender *render, const BufferCreateInfo &ci);

  protected:
    IBuffer()                            = default;
    virtual void mapInternal(void **ptr) = 0;
};

} // namespace ya
