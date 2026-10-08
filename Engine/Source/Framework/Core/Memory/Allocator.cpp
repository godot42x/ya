#include "Core/Memory/Allocator.h"

#include <cstdint>
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

/// What every block this layer hands out carries in front of it: how many
/// bytes the caller asked for, and how far the *header* sits above the block
/// the backend returned (zero unless the request was over-aligned). One rule
/// recovers the base for both cases:
/// `base = pointer - kAllocationHeaderSize - baseOffset`.
struct FAllocationHeader
{
    std::size_t size;
    std::size_t baseOffset;
};

/// Sized to keep the user pointer at least `alignof(std::max_align_t)` when it
/// sits immediately after the header.
constexpr std::size_t kAllocationHeaderSize = alignof(std::max_align_t) > sizeof(FAllocationHeader)
                                                  ? alignof(std::max_align_t)
                                                  : sizeof(FAllocationHeader);

/// The header in front of `pointer`, or null when the pointer cannot be one of
/// ours. A null here means the caller freed something this layer did not hand
/// out; deallocate falls back to the sized-delete hint.
[[nodiscard]] const FAllocationHeader* headerOf(void* pointer)
{
    return reinterpret_cast<const FAllocationHeader*>(static_cast<char*>(pointer) - kAllocationHeaderSize);
}

} // namespace

void* MallocAllocator::allocate(std::size_t size, std::size_t alignment)
{
    // The standard requires a distinct, non-null pointer for a zero-size
    // request, so zero is rounded to one byte rather than handed to malloc.
    const std::size_t bytes = size == 0 ? 1 : size;
    // Only genuinely over-aligned requests go to aligned_alloc. malloc already
    // satisfies alignof(max_align_t), and the platform's aligned_alloc rejects
    // any alignment below sizeof(void*) -- returning null, not rounding. An
    // alignof(T) of 1, 2 or 4 is perfectly ordinary (glm::mat4 is one), so a
    // caller asking for `operator new(size, align_val_t(4))` must not be handed
    // a failure the platform allocator would have satisfied.
    if (alignment <= alignof(std::max_align_t)) {
        return std::malloc(bytes);
    }
    // aligned_alloc also demands a size that is a multiple of the alignment.
    return std::aligned_alloc(alignment, alignUp(bytes, alignment));
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
    // A size header in front of every block, so `deallocate` knows how many
    // bytes to give back without the caller telling it. Without it the live
    // level only ever grows, because most calls through the global
    // `operator delete` carry no size -- a "level" that is really a running
    // total is worse than no number at all.
    //
    // This is what every real allocator does, and it is why the accounting can
    // answer "how much is live per subsystem" instead of "how much was ever
    // allocated per subsystem".
    if (alignment <= alignof(std::max_align_t)) {
        // malloc already meets max_align_t, so the header can sit immediately
        // in front of the block and the base is the pointer minus the header.
        void* base = ProcessAllocator::allocate(kAllocationHeaderSize + size, 0);
        if (base == nullptr) {
            return nullptr;
        }
        FAllocationHeader* header = static_cast<FAllocationHeader*>(base);
        header->size              = size;
        header->baseOffset        = 0;
        return static_cast<char*>(base) + kAllocationHeaderSize;
    }

    // Over-aligned: reserve extra room and place the user pointer at an aligned
    // address inside it, recording how far it moved so deallocate can find the
    // base again.
    void* base = ProcessAllocator::allocate(kAllocationHeaderSize + size + alignment, alignment);
    if (base == nullptr) {
        return nullptr;
    }
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
    const auto userAddress = alignUp(baseAddress + kAllocationHeaderSize, alignment);
    auto*      aligned     = reinterpret_cast<char*>(userAddress);
    auto*      header      = reinterpret_cast<FAllocationHeader*>(aligned - kAllocationHeaderSize);
    header->size           = size;
    // Measured from the header, not from the base, so both paths share one
    // recovery rule: base = pointer - kAllocationHeaderSize - baseOffset.
    // Measured from the base it would be off by the header on every block.
    header->baseOffset = static_cast<std::size_t>(userAddress - baseAddress) - kAllocationHeaderSize;
    return aligned;
}

void processDeallocate(void* pointer, std::size_t size) noexcept
{
    if (pointer == nullptr) {
        return;
    }

    // Read the header this layer wrote. A pointer it did not hand out has no
    // header, which is outside the contract; the hint is the best available
    // answer for it and nothing here may crash trying.
    const FAllocationHeader* header = headerOf(pointer);
    const std::size_t live         = header != nullptr ? header->size : size;
    const std::size_t baseOffset   = header != nullptr ? header->baseOffset : 0;

    ProcessAllocator::deallocate(static_cast<char*>(pointer) - kAllocationHeaderSize - baseOffset, live);
}

FAllocatorStats allocatorStatsSnapshot()
{
    return ProcessAllocator::stats();
}

} // namespace ya::memory
