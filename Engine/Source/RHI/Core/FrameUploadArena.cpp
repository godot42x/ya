#include "FrameUploadArena.h"

#include "Core/Log.h"
#include "Resource/DeferredDeletionQueue.h"

#include <algorithm>
#include <limits>

namespace ya
{

namespace
{

bool checkedAdd(uint64_t lhs, uint64_t rhs, uint64_t& result)
{
    if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

uint64_t alignUp(uint64_t value, uint32_t alignment)
{
    const uint64_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

} // namespace

bool FrameUploadArena::Allocation::write(const void* data, uint32_t bytes) const
{
    if (!valid() || !data || offset > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    const uint64_t writeSize = bytes == 0 ? size : bytes;
    if (writeSize == 0 || writeSize > size || writeSize > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    return buffer->writeData(
        data,
        static_cast<uint32_t>(writeSize),
        static_cast<uint32_t>(offset));
}

FrameUploadArena::FrameUploadArena(
    IRenderResourceFactory& factory,
    uint32_t               flightCount,
    uint32_t               initialCapacity,
    EBufferUsage           usage,
    std::string            label)
    : _factory(factory),
      _flights(flightCount),
      _initialCapacity(initialCapacity),
      _usage(usage),
      _label(std::move(label))
{
    if (flightCount == 0) {
        YA_CORE_WARN("FrameUploadArena '{}' was created with zero flights", _label);
    }
}

FrameUploadArena::~FrameUploadArena()
{
    for (auto& flight : _flights) {
        if (!flight.backing) {
            continue;
        }

        auto backing = std::move(flight.backing);
        if (DeferredDeletionQueue::get().isInitialized()) {
            DeferredDeletionQueue::get().retireResource(std::move(backing));
        }
        else {
            backing.reset();
        }
        flight.capacity = 0;
        flight.cursor   = 0;
    }
}

bool FrameUploadArena::beginFlight(uint32_t flightIndex)
{
    if (flightIndex >= _flights.size()) {
        YA_CORE_WARN("FrameUploadArena::beginFlight invalid flight index {}", flightIndex);
        return false;
    }

    // The caller owns the fence wait. Resetting here is safe only after the
    // corresponding flight has completed on the GPU.
    _flights[flightIndex].cursor = 0;
    return true;
}

stdptr<IBuffer> FrameUploadArena::createBacking(uint32_t flightIndex, uint32_t capacity) const
{
    const std::string bufferLabel = _label + ".flight" + std::to_string(flightIndex);
    auto backing = _factory.createBuffer(BufferCreateInfo{
        .label       = bufferLabel,
        .usage       = _usage,
        .data        = std::nullopt,
        .size        = capacity,
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });

    if (!backing) {
        YA_CORE_ERROR("FrameUploadArena failed to create backing buffer '{}'", bufferLabel);
        return nullptr;
    }
    if (!backing->isHostVisible()) {
        YA_CORE_ERROR("FrameUploadArena backing buffer '{}' is not host visible", bufferLabel);
        return nullptr;
    }
    return backing;
}

bool FrameUploadArena::ensureCapacity(uint32_t flightIndex, uint64_t requiredEnd)
{
    if (flightIndex >= _flights.size() || requiredEnd == 0 ||
        requiredEnd > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    auto& flight = _flights[flightIndex];
    if (requiredEnd <= flight.capacity && flight.backing) {
        return true;
    }

    uint64_t targetCapacity = flight.capacity != 0 ? flight.capacity : _initialCapacity;
    if (targetCapacity == 0) {
        targetCapacity = requiredEnd;
    }
    while (targetCapacity < requiredEnd) {
        if (targetCapacity > std::numeric_limits<uint32_t>::max() / 2u) {
            targetCapacity = requiredEnd;
            break;
        }
        targetCapacity *= 2u;
    }

    if (targetCapacity > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("FrameUploadArena allocation exceeds 32-bit buffer size");
        return false;
    }

    const uint32_t newCapacity = static_cast<uint32_t>(targetCapacity);
    auto           newBacking  = createBacking(flightIndex, newCapacity);
    if (!newBacking) {
        return false;
    }

    auto oldBacking = std::move(flight.backing);
    flight.backing  = std::move(newBacking);
    flight.capacity = newCapacity;

    if (oldBacking) {
        if (DeferredDeletionQueue::get().isInitialized()) {
            DeferredDeletionQueue::get().retireResource(std::move(oldBacking));
        }
        else {
            oldBacking.reset();
        }
    }
    return true;
}

std::optional<FrameUploadArena::Allocation> FrameUploadArena::allocate(
    uint32_t flightIndex,
    uint32_t size,
    uint32_t alignment)
{
    if (flightIndex >= _flights.size() || size == 0 || alignment == 0) {
        YA_CORE_WARN(
            "FrameUploadArena rejected allocation (flight={}, size={}, alignment={})",
            flightIndex,
            size,
            alignment);
        return std::nullopt;
    }

    auto&   flight      = _flights[flightIndex];
    const auto aligned  = alignUp(flight.cursor, alignment);
    uint64_t requiredEnd = 0;
    if (!checkedAdd(aligned, size, requiredEnd) || !ensureCapacity(flightIndex, requiredEnd)) {
        YA_CORE_ERROR("FrameUploadArena failed to grow flight {} for {} bytes", flightIndex, size);
        return std::nullopt;
    }

    // ensureCapacity may create a backing buffer but never changes the cursor.
    flight.cursor = static_cast<uint32_t>(requiredEnd);
    return Allocation{
        .buffer = flight.backing,
        .offset = aligned,
        .size   = size,
    };
}

uint32_t FrameUploadArena::capacity(uint32_t flightIndex) const
{
    return flightIndex < _flights.size() ? _flights[flightIndex].capacity : 0;
}

uint32_t FrameUploadArena::bytesUsed(uint32_t flightIndex) const
{
    return flightIndex < _flights.size() ? _flights[flightIndex].cursor : 0;
}

stdptr<IBuffer> FrameUploadArena::backingBuffer(uint32_t flightIndex) const
{
    return flightIndex < _flights.size() ? _flights[flightIndex].backing : nullptr;
}

} // namespace ya
