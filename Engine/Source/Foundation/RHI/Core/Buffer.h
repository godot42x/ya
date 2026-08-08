// Buffer.h - Generic buffer interface for multi-backend rendering
#pragma once

#include "Foundation/Core/Base.h"
#include "Foundation/RHI/Core/Handle.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ya
{

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

// Memory usage intent (backend-agnostic, drives allocator strategy)
enum class EMemoryUsage : uint8_t
{
    Auto,      // Backend picks optimal memory (default)
    GpuOnly,   // GPU-exclusive, CPU cannot access (vertex/index/static data)
    CpuToGpu,  // CPU writes sequentially, GPU reads (UBO, staging, dynamic vertex)
    GpuToCpu,  // GPU writes, CPU reads back (readback, queries)
};

struct BufferCreateInfo
{
    std::string           label;
    EBufferUsage          usage;
    std::optional<void *> data = std::nullopt;
    uint32_t              size;
    EMemoryUsage          memoryUsage = EMemoryUsage::Auto;
};

/// Immutable buffer creation spec.
///
/// `isSameBufferCreateInfo` compares the resource identity/spec only: label,
/// usage, size and memory class. The transient `data` upload pointer is
/// deliberately excluded — it is a creation-time hint, not part of the
/// persistent spec. Replacement must create a new IBuffer (and retire the old
/// one through a completion-safe owner), never resize an existing object.
inline bool isSameBufferCreateInfo(const BufferCreateInfo& lhs, const BufferCreateInfo& rhs)
{
    return lhs.label == rhs.label &&
           lhs.usage == rhs.usage &&
           lhs.size == rhs.size &&
           lhs.memoryUsage == rhs.memoryUsage;
}


struct BufferHandleTag
{};
using BufferHandle = Handle<BufferHandleTag>;

// Generic buffer interface
struct IBuffer
{
  public:
    virtual ~IBuffer() = default;

    // Disable copy
    IBuffer(const IBuffer &)            = delete;
    IBuffer &operator=(const IBuffer &) = delete;

    // Enable move
    IBuffer(IBuffer &&)            = default;
    IBuffer &operator=(IBuffer &&) = default;

    // ═══════════════════════════════════════════════════════════════════
    // Data-access contract (FG-802). Backends must follow these semantics:
    //
    // * writeData(data, size, offset):
    //     - returns false (and writes nothing) when data is null, the buffer
    //       is not host visible, or offset+size exceeds the buffer size;
    //     - size == 0 means "whole buffer" and requires offset == 0;
    //     - otherwise performs a full range write and returns true.
    // * flush(size, offset):
    //     - returns false when the buffer is not host visible, is not
    //       currently mapped, or the range is out of bounds;
    //     - is a no-op success on coherent memory;
    //     - size == 0 means "whole buffer" and requires offset == 0.
    // * map<T>() / unmap():
    //     - map requires a host-visible buffer with no active map (backends
    //       report this via assert/error and return null);
    //     - unmap is idempotent;
    //     - for non-coherent readback (GpuToCpu), map makes prior GPU writes
    //       visible to the CPU (backend invalidates on map).
    // ═══════════════════════════════════════════════════════════════════
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

    // Get declared buffer usage flags
    virtual EBufferUsage getUsage() const = 0;

    // Check if buffer is host visible
    virtual bool isHostVisible() const = 0;

    // Get buffer name/label
    virtual const std::string &getName() const = 0;

  protected:
    IBuffer()                            = default;
    virtual void mapInternal(void **ptr) = 0;
};

} // namespace ya
