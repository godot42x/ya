#pragma once

#include "Core/Api.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderResourceFactory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

/**
 * Linear CPU-to-GPU buffer allocator for frame-local data.
 *
 * Each flight owns an independent backing buffer. The caller must invoke
 * beginFlight() only after that flight's GPU fence has been waited, typically
 * from IRender::begin(). A backing buffer may grow while a frame is being
 * recorded; the old buffer is retired through DeferredDeletionQueue so command
 * buffers recorded earlier in the same frame remain valid until submission.
 *
 * The arena deliberately does not infer backend alignment limits. Callers pass
 * the alignment required by the descriptor or storage-buffer contract.
 */
class ENGINE_API FrameUploadArena
{
  public:
    struct ENGINE_API Allocation
    {
        stdptr<IBuffer> buffer;
        uint64_t         offset = 0;
        uint64_t         size   = 0;

        [[nodiscard]] bool valid() const { return buffer != nullptr && size != 0; }
        explicit operator bool() const { return valid(); }

        [[nodiscard]] BufferHandle bufferHandle() const
        {
            return buffer ? buffer->getHandle() : BufferHandle{};
        }

        [[nodiscard]] DescriptorBufferInfo descriptor() const
        {
            return DescriptorBufferInfo{bufferHandle(), offset, size};
        }

        /// Copy CPU data into this slice. The default writes the whole slice.
        bool write(const void* data, uint32_t bytes = 0) const;
    };

    /**
     * @param factory      Resource factory used to create host-visible buffers.
     * @param flightCount  Number of independently reusable frame flights.
     * @param initialCapacity Initial backing size per flight. Zero enables lazy
     *                        creation sized to the first allocation.
     * @param usage        Buffer usage union required by future consumers.
     * @param label        Debug label prefix for backing buffers.
     */
    explicit FrameUploadArena(
        IRenderResourceFactory& factory,
        uint32_t               flightCount,
        uint32_t               initialCapacity = 64u * 1024u,
        EBufferUsage           usage            = EBufferUsage::UniformBuffer | EBufferUsage::StorageBuffer,
        std::string            label            = "FrameUploadArena");
    ~FrameUploadArena();

    FrameUploadArena(const FrameUploadArena&)            = delete;
    FrameUploadArena& operator=(const FrameUploadArena&) = delete;
    FrameUploadArena(FrameUploadArena&&)                 = delete;
    FrameUploadArena& operator=(FrameUploadArena&&)      = delete;

    /**
     * Reset the allocation cursor for a flight whose fence has completed.
     * Returns false for an invalid flight index.
     */
    bool beginFlight(uint32_t flightIndex);

    /**
     * Allocate an aligned slice from the current flight backing buffer.
     * Alignment is explicit and must be greater than zero; no backend-specific
     * alignment is assumed by this class.
     */
    [[nodiscard]] std::optional<Allocation> allocate(
        uint32_t flightIndex,
        uint32_t size,
        uint32_t alignment);

    [[nodiscard]] uint32_t flightCount() const { return static_cast<uint32_t>(_flights.size()); }
    [[nodiscard]] uint32_t capacity(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t bytesUsed(uint32_t flightIndex) const;
    [[nodiscard]] stdptr<IBuffer> backingBuffer(uint32_t flightIndex) const;

  private:
    struct Flight
    {
        stdptr<IBuffer> backing;
        uint32_t         capacity = 0;
        uint32_t         cursor   = 0;
    };

    IRenderResourceFactory& _factory;
    std::vector<Flight>     _flights;
    uint32_t                _initialCapacity = 0;
    EBufferUsage            _usage            = EBufferUsage::None;
    std::string             _label;

    [[nodiscard]] bool ensureCapacity(uint32_t flightIndex, uint64_t requiredEnd);
    [[nodiscard]] stdptr<IBuffer> createBacking(uint32_t flightIndex, uint32_t capacity) const;
};

} // namespace ya
