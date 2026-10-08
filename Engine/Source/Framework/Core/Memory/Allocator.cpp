#include "Core/Memory/Allocator.h"

#include <cstdlib>

namespace ya::memory
{

namespace
{

/// The owner a thread charges its allocations to. Trivially initialized, so
/// there is no dynamic-initialization order to get wrong: the process
/// allocator may run before any of that has happened.
thread_local EMemoryOwner t_owner = EMemoryOwner::Untagged;

[[nodiscard]] std::size_t alignUp(std::size_t size, std::size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace

void* MallocAllocator::allocate(std::size_t size, std::size_t alignment)
{
    // The standard requires a distinct, non-null pointer for a zero-size
    // request, so zero is rounded to one byte rather than handed to malloc.
    const std::size_t bytes = size == 0 ? 1 : size;
    if (alignment != 0) {
        // aligned_alloc demands a size that is a multiple of the alignment.
        return std::aligned_alloc(alignment, alignUp(bytes, alignment));
    }
    return std::malloc(bytes);
}

void MallocAllocator::deallocate(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

std::string_view toMemoryOwnerLabel(EMemoryOwner owner)
{
    switch (owner) {
    case EMemoryOwner::Untagged:
        return "untagged";
    case EMemoryOwner::Resource:
        return "resource";
    case EMemoryOwner::Render:
        return "render";
    case EMemoryOwner::Script:
        return "script";
    }
    return "unknown";
}

MemoryOwnerScope::MemoryOwnerScope(EMemoryOwner owner)
    : _previous(t_owner)
{
    t_owner = owner;
}

MemoryOwnerScope::~MemoryOwnerScope()
{
    t_owner = _previous;
}

template <AllocatorBackend Backend>
EMemoryOwner CountingAllocator<Backend>::currentOwner()
{
    return t_owner;
}

template <AllocatorBackend Backend>
FAllocatorStats CountingAllocator<Backend>::stats()
{
    FAllocatorStats stats;
    stats.bSupported = true;
    for (uint32_t owner = 0; owner < kMemoryOwnerCount; ++owner) {
        FAllocatorBucketStats& bucket = stats.buckets[owner];
        bucket.liveBytes       = s_liveBytes[owner].load(std::memory_order_relaxed);
        bucket.liveAllocations = s_liveAllocations[owner].load(std::memory_order_relaxed);
        bucket.totalAllocCount = s_totalAllocCount[owner].load(std::memory_order_relaxed);
        bucket.totalAllocBytes = s_totalAllocBytes[owner].load(std::memory_order_relaxed);
        bucket.totalFreeCount  = s_totalFreeCount[owner].load(std::memory_order_relaxed);
        bucket.totalFreeBytes  = s_totalFreeBytes[owner].load(std::memory_order_relaxed);
    }
    return stats;
}

// The counting instantiation this build routes through. Explicitly
// instantiated here so its counters exist in exactly one image (Core) and
// `allocatorStatsSnapshot` reads the same set `processAllocate` updates.
template struct CountingAllocator<MallocAllocator>;

void* processAllocate(std::size_t size, std::size_t alignment)
{
    return ProcessAllocator::allocate(size, alignment);
}

void processDeallocate(void* pointer, std::size_t size) noexcept
{
    ProcessAllocator::deallocate(pointer, size);
}

FAllocatorStats allocatorStatsSnapshot()
{
    return ProcessAllocator::stats();
}

} // namespace ya::memory
