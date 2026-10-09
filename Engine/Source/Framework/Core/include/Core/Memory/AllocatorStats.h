#pragma once

#include "Core/Api.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ya::memory
{

/// Which subsystem's bucket an allocation is charged to.
///
/// A C++ allocation site does not know its owner, so attribution comes from
/// the scope that is running: `MemoryOwnerScope` (or `YA_MEMORY_OWNER`)
/// charges everything allocated on this thread until it goes away, and
/// everything outside any scope lands in `Untagged` -- reported, never
/// hidden.
enum class EMemoryOwner : uint8_t
{
    Untagged = 0,
    /// Content pipeline: asset loading, decoding, upload staging, caches.
    Resource,
    /// Render runtime: render graph, pipelines, passes, frame allocators.
    Render,
    /// Script VM and the values crossing into it.
    Script,
};

inline constexpr uint32_t kMemoryOwnerCount = 4;

[[nodiscard]] YA_CORE_API std::string_view toMemoryOwnerLabel(EMemoryOwner owner);

/// One bucket's counters.
///
/// `live*` is a level; `total*` are monotonic and only ever grow, so a
/// benchmark sample takes differences of them rather than resetting anything
/// -- a counter reset at the wrong frame boundary silently folds a whole
/// warmup into one frame.
struct FAllocatorBucketStats
{
    uint64_t liveBytes       = 0;
    uint64_t liveAllocations = 0;
    uint64_t totalAllocCount = 0;
    uint64_t totalAllocBytes = 0;
    uint64_t totalFreeCount  = 0;
    uint64_t totalFreeBytes  = 0;
};

/// What an allocator has done, per owner bucket.
struct FAllocatorStats
{
    /// False when nothing is counting -- no counting allocator is compiled
    /// in as the process allocator. Callers print "n/a" rather than a row of
    /// zeros that would read as a measurement.
    bool                                                 bSupported = false;
    /// False once the size side table ran out of slots. The counters still
    /// count, but the *size* of anything allocated after that point comes
    /// from the sized-delete hint alone (which is usually absent), so the live
    /// byte level is a floor rather than a measurement. Reported instead of
    /// hidden: a truncated table that reads as a level is the exact failure
    /// the table exists to prevent.
    ///
    /// Always true in a build that does not count -- there is no table to fill.
    bool                                                 sizeTableComplete = true;
    std::array<FAllocatorBucketStats, kMemoryOwnerCount> buckets{};

    [[nodiscard]] uint64_t getLiveBytes() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.liveBytes;
        }
        return total;
    }

    [[nodiscard]] uint64_t getLiveAllocations() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.liveAllocations;
        }
        return total;
    }

    [[nodiscard]] uint64_t getTotalAllocCount() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.totalAllocCount;
        }
        return total;
    }

    [[nodiscard]] uint64_t getTotalAllocBytes() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.totalAllocBytes;
        }
        return total;
    }

    [[nodiscard]] uint64_t getTotalFreeCount() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.totalFreeCount;
        }
        return total;
    }

    [[nodiscard]] uint64_t getTotalFreeBytes() const
    {
        uint64_t total = 0;
        for (const FAllocatorBucketStats& bucket : buckets) {
            total += bucket.totalFreeBytes;
        }
        return total;
    }

    /// `current - previous`, per bucket. Requires `previous` to be a snapshot
    /// taken earlier from the same allocator; `live*` fields carry the current
    /// level, the totals the difference.
    [[nodiscard]] FAllocatorStats deltaSince(const FAllocatorStats& previous) const
    {
        FAllocatorStats delta;
        delta.bSupported        = bSupported && previous.bSupported;
        // A level, like the `live*` fields: it describes *now*, so it carries
        // over rather than being differenced. Defaulting it to true here would
        // report a full table over a truncated one.
        delta.sizeTableComplete = sizeTableComplete;
        for (uint32_t owner = 0; owner < kMemoryOwnerCount; ++owner) {
            const FAllocatorBucketStats& now   = buckets[owner];
            const FAllocatorBucketStats& then  = previous.buckets[owner];
            FAllocatorBucketStats&       out   = delta.buckets[owner];
            out.liveBytes       = now.liveBytes;
            out.liveAllocations = now.liveAllocations;
            out.totalAllocCount = now.totalAllocCount - then.totalAllocCount;
            out.totalAllocBytes = now.totalAllocBytes - then.totalAllocBytes;
            out.totalFreeCount  = now.totalFreeCount - then.totalFreeCount;
            out.totalFreeBytes  = now.totalFreeBytes - then.totalFreeBytes;
        }
        return delta;
    }
};

/// Charge allocations made on this thread to `owner` until this goes away.
/// Nested scopes restore the enclosing owner, so a subsystem can be scoped
/// inside another without special-casing.
///
/// Out of line (in Core) on purpose: the owner is a thread-local living in
/// Core, and every module must set the same one, not a copy of it.
class YA_CORE_API MemoryOwnerScope
{
    EMemoryOwner _previous = EMemoryOwner::Untagged;

  public:
    explicit MemoryOwnerScope(EMemoryOwner owner);
    ~MemoryOwnerScope();

    MemoryOwnerScope(const MemoryOwnerScope&)            = delete;
    MemoryOwnerScope& operator=(const MemoryOwnerScope&) = delete;
};

#define YA_MEMORY_OWNER(owner)                                                       \
    ::ya::memory::MemoryOwnerScope yaMemoryOwnerScope_##__LINE__(                    \
        ::ya::memory::EMemoryOwner::owner)

} // namespace ya::memory
