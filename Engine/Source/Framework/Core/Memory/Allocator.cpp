#include "Core/Memory/Allocator.h"

#include <atomic>
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

// ---------------------------------------------------------------------------
// Size side table
//
// Every allocation records the size the caller asked for, because almost every
// call through the global `operator delete` carries none and without a record
// "live bytes" is really a running total.
//
// It is a table rather than a header in front of the block on purpose. A header
// makes our pointers *not* malloc pointers: the block the platform (or another
// image that never went through this layer) has to free is offset from what we
// returned, and any cross-boundary free -- a third-party dylib releasing
// something we allocated, or one of our images freeing a block another image
// allocated -- would hand `free` a pointer it never saw. Keeping the pointer
// pristine costs one hash and one probe per allocation and makes the layer
// invisible at the boundary in both directions.
// ---------------------------------------------------------------------------

#ifdef YA_BUILD_MODE_PROFILE

/// Open addressing with linear probing, insert-only: an entry is never removed,
/// because a freed address is reused rather than abandoned, so re-recording the
/// same key overwrites it. Tombstones and rehash are what deletion would cost,
/// and neither buys anything here.
struct FSizeSlot
{
    uintptr_t key = 0; // 0 means empty
    uint64_t  size = 0;
};

/// Sized to the peak number of *distinct addresses* a run holds at once, not to
/// the number of allocations it makes: an address that is freed and handed out
/// again is one entry for the whole run. The measured peak for the 2D/3D
/// PerfLab scenes is about 300k live blocks; this leaves headroom without
/// spending real memory (untouched pages of a zero-filled array cost nothing).
constexpr uint32_t kSizeTableBits  = 19;
constexpr uint32_t kSizeTableSlots = 1u << kSizeTableBits;
constexpr uint64_t kSizeTableMask  = kSizeTableSlots - 1;

FSizeSlot g_sizeTable[kSizeTableSlots];
std::atomic_flag g_sizeTableLock = ATOMIC_FLAG_INIT;
/// Set once the table is full: accounting degrades to the sized-delete hint
/// instead of lying, and the report says so.
std::atomic<bool> g_sizeTableFull{false};

[[nodiscard]] uint64_t sizeSlotOf(const void* pointer)
{
    const uint64_t key = reinterpret_cast<uintptr_t>(pointer);
    // malloc hands out 16-byte-aligned blocks; skipping those low bits keeps
    // the low bits of the hash from being constant.
    return (key >> 4) * 0x9E3779B97F4A7C15ull;
}

void lockSizeTable() noexcept
{
    while (g_sizeTableLock.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#endif
    }
}

void unlockSizeTable() noexcept
{
    g_sizeTableLock.clear(std::memory_order_release);
}

void recordAllocationSize(void* pointer, std::size_t size) noexcept
{
    const uintptr_t key = reinterpret_cast<uintptr_t>(pointer);
    if (key == 0) {
        return;
    }
    lockSizeTable();
    uint64_t index = sizeSlotOf(pointer) & kSizeTableMask;
    for (uint32_t probe = 0; probe < kSizeTableSlots; ++probe) {
        FSizeSlot& slot = g_sizeTable[index];
        if (slot.key == 0) {
            slot.key  = key;
            slot.size = size;
            unlockSizeTable();
            return;
        }
        if (slot.key == key) {
            // Same address handed out again: the old entry is stale by
            // definition, so overwriting is the whole update.
            slot.size = size;
            unlockSizeTable();
            return;
        }
        index = (index + 1) & kSizeTableMask;
    }
    g_sizeTableFull.store(true, std::memory_order_relaxed);
    unlockSizeTable();
}

/// The size recorded for `pointer`, or false when the table has no entry for
/// it (a pointer this layer never handed out, or one recorded before the table
/// was full).
[[nodiscard]] bool lookupAllocationSize(const void* pointer, std::size_t& outSize) noexcept
{
    const uintptr_t key = reinterpret_cast<uintptr_t>(pointer);
    if (key == 0) {
        return false;
    }
    lockSizeTable();
    uint64_t index = sizeSlotOf(pointer) & kSizeTableMask;
    bool     found = false;
    for (uint32_t probe = 0; probe < kSizeTableSlots; ++probe) {
        const FSizeSlot& slot = g_sizeTable[index];
        if (slot.key == key) {
            outSize = static_cast<std::size_t>(slot.size);
            found   = true;
            break;
        }
        if (slot.key == 0) {
            break;
        }
        index = (index + 1) & kSizeTableMask;
    }
    unlockSizeTable();
    return found;
}

[[nodiscard]] bool sizeTableFull() noexcept
{
    return g_sizeTableFull.load(std::memory_order_relaxed);
}

#else

// Not counting: no sizes to keep, and nothing between the caller and malloc.

void  recordAllocationSize(void*, std::size_t) noexcept {}
bool  lookupAllocationSize(const void*, std::size_t&) noexcept { return false; }
bool  sizeTableFull() noexcept { return false; }

#endif // YA_BUILD_MODE_PROFILE

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
    // The pointer handed back is exactly the block the backend returned: no
    // header, no offset. Whoever frees it -- this layer, another of our images,
    // or a third-party library that owns it -- is handing `free` a pointer it
    // really was given.
    void* pointer = ProcessAllocator::allocate(size, alignment);
    if (pointer != nullptr) {
        recordAllocationSize(pointer, size);
    }
    return pointer;
}

void processDeallocate(void* pointer, std::size_t size) noexcept
{
    if (pointer == nullptr) {
        return;
    }

    // The recorded size is the one to give back; the hint covers a pointer this
    // layer never handed out, and must not crash trying.
    std::size_t live = size;
    (void)lookupAllocationSize(pointer, live);

    ProcessAllocator::deallocate(pointer, live);
}

FAllocatorStats allocatorStatsSnapshot()
{
    FAllocatorStats stats = ProcessAllocator::stats();
    // False means some allocations could not be recorded and their live bytes
    // are only as good as the sized-delete hints. Reported rather than hidden.
    stats.sizeTableComplete = !sizeTableFull();
    return stats;
}

} // namespace ya::memory
