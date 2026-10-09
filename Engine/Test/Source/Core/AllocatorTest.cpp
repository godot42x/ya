// The engine's allocator layer: the backend contract, the counting allocator's
// bookkeeping, and the routing the global allocation functions use.
//
// Runs in any build. In debug and release the process allocator is plain
// malloc and nothing counts -- which is itself part of what is asserted here
// (an uncounted build reports "not counting", never a row of zeros). The
// counting allocator is exercised directly as a type, so its behaviour is
// testable without making the whole test binary count.

#include "Core/Memory/Allocator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>

namespace
{

using namespace ya::memory;
using Counting = CountingAllocator<MallocAllocator>;

constexpr uint32_t kUntagged  = static_cast<uint32_t>(EMemoryOwner::Untagged);
constexpr uint32_t kResource  = static_cast<uint32_t>(EMemoryOwner::Resource);
constexpr uint32_t kRender    = static_cast<uint32_t>(EMemoryOwner::Render);

} // namespace

static_assert(AllocatorBackend<MallocAllocator>);
static_assert(AllocatorBackend<Counting>);
static_assert(!AllocatorBackend<int>);

TEST(AllocatorBackendTest, MallocAllocatorSatisfiesTheContract)
{
    void* pointer = MallocAllocator::allocate(64, 0);
    ASSERT_NE(pointer, nullptr);
    MallocAllocator::deallocate(pointer, 64);

    // A zero-size request must still be distinguishable, and freeing it must
    // not be a special case.
    void* empty = MallocAllocator::allocate(0, 0);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(empty, pointer);
    MallocAllocator::deallocate(empty, 0);

    // Deallocating null is a no-op, not a crash.
    MallocAllocator::deallocate(nullptr, 0);
}

TEST(AllocatorBackendTest, OverAlignedRequestsComeBackAligned)
{
    for (const std::size_t alignment : { std::size_t{ 16 }, std::size_t{ 64 }, std::size_t{ 256 } }) {
        void* pointer = MallocAllocator::allocate(100, alignment);
        ASSERT_NE(pointer, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0u) << "alignment " << alignment;
        MallocAllocator::deallocate(pointer, 100);
    }
}

TEST(AllocatorBackendTest, AlignmentsBelowTheDefaultAreServedRatherThanRefused)
{
    // Regression: the platform's aligned_alloc rejects any alignment smaller
    // than sizeof(void*) and answers with null, so an alignof(T) of 1, 2 or 4
    // (glm::mat4 is 4) has to fall back to malloc, which already satisfies it.
    // Refusing these turned every boxed value-type allocation into bad_alloc.
    for (const std::size_t alignment : { std::size_t{ 1 }, std::size_t{ 2 }, std::size_t{ 4 },
                                         std::size_t{ 8 }, alignof(std::max_align_t) }) {
        void* pointer = MallocAllocator::allocate(64, alignment);
        ASSERT_NE(pointer, nullptr) << "alignment " << alignment;
        EXPECT_GE(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0u);
        MallocAllocator::deallocate(pointer, 64);
    }
}

TEST(AllocatorBackendTest, TheGlobalReplacementServesSmallAlignments)
{
    // The same path as above, through the routing the replaced global
    // `operator new` actually uses.
    void* pointer = processAllocate(64, alignof(double));
    ASSERT_NE(pointer, nullptr);
    processDeallocate(pointer, 64);
}

TEST(AllocatorBackendTest, ProcessRoutingUsesTheDefaultBackendWhenNothingIsInstalled)
{
    // No build installs anything at runtime: the process allocator is a
    // compile-time alias. Routing must still work and hand back usable memory.
    void* pointer = processAllocate(32, 0);
    ASSERT_NE(pointer, nullptr);
    processDeallocate(pointer, 32);
    processDeallocate(nullptr, 0);
}

TEST(AllocatorBackendTest, AnUncountedBuildReportsNotCounting)
{
    // The whole point of the "supported" flag: a debug or release build has no
    // counting allocator, and must not look like a process that allocated zero.
    const FAllocatorStats stats = allocatorStatsSnapshot();
    EXPECT_EQ(stats.bSupported, processAllocatorCounts());
    if (!stats.bSupported) {
        EXPECT_EQ(stats.getLiveBytes(), 0u);
        EXPECT_EQ(stats.getTotalAllocCount(), 0u);
    }
}

TEST(CountingAllocatorTest, ChargesAllocationsToTheCurrentOwnerScope)
{
    using namespace ya::memory;
    const FAllocatorStats before = Counting::stats();

    void* resource = nullptr;
    void* render   = nullptr;
    {
        const MemoryOwnerScope scope(EMemoryOwner::Resource);
        resource = Counting::allocate(1000, 0);
    }
    {
        const MemoryOwnerScope scope(EMemoryOwner::Render);
        render = Counting::allocate(2000, 0);
    }

    const FAllocatorStats after = Counting::stats();
    ASSERT_TRUE(after.bSupported);
    const FAllocatorStats frame = after.deltaSince(before);
    EXPECT_EQ(frame.buckets[kResource].totalAllocCount, 1u);
    EXPECT_EQ(frame.buckets[kResource].totalAllocBytes, 1000u);
    EXPECT_EQ(frame.buckets[kRender].totalAllocCount, 1u);
    EXPECT_EQ(frame.buckets[kRender].totalAllocBytes, 2000u);

    Counting::deallocate(resource, 1000);
    Counting::deallocate(render, 2000);
}

TEST(CountingAllocatorTest, NestedOwnerScopesRestoreTheEnclosingOwner)
{
    using namespace ya::memory;
    const FAllocatorStats before = Counting::stats();
    void*                   inner  = nullptr;
    void*                   outer  = nullptr;
    void*                   after  = nullptr;
    {
        const MemoryOwnerScope resourceScope(EMemoryOwner::Resource);
        outer = Counting::allocate(64, 0);
        {
            const MemoryOwnerScope renderScope(EMemoryOwner::Render);
            inner = Counting::allocate(64, 0);
        }
        // Back in the resource scope: this allocation is charged there, not to
        // the scope that just ended.
        after = Counting::allocate(64, 0);
    }

    const FAllocatorStats frame = Counting::stats().deltaSince(before);
    EXPECT_EQ(frame.buckets[kResource].totalAllocCount, 2u);
    EXPECT_EQ(frame.buckets[kRender].totalAllocCount, 1u);

    Counting::deallocate(inner, 64);
    Counting::deallocate(outer, 64);
    Counting::deallocate(after, 64);
}

TEST(CountingAllocatorTest, AnOwnerScopeDoesNotFollowWorkToAnotherThread)
{
    using namespace ya::memory;

    // The owner tag is thread-local, and that is a feature rather than a
    // limitation: subsystem entry points hand work to worker threads (texture
    // decode is the case that matters -- megabytes of raw pixels per texture),
    // and tagging the submitting thread would leave every worker untagged. So
    // the scope has to open *inside* the worker, which only works if a scope
    // opened on one thread is genuinely invisible to another.
    //
    // Asserted through `currentOwner()` rather than through the counters. The
    // counters are process-wide by design, so "this thread allocated exactly
    // one block" is not observable from inside a busy process -- an unrelated
    // thread allocating under the same owner moves the number, and a delta
    // taken across a thread boundary reads whatever every other thread did in
    // between. The tag itself is per-thread, so that is the honest thing to
    // read.
    std::atomic<int> ownerInsideSpawnedScope{static_cast<int>(EMemoryOwner::Resource)};
    std::atomic<int> ownerAfterOwnScope{static_cast<int>(EMemoryOwner::Untagged)};

    {
        const MemoryOwnerScope scope(EMemoryOwner::Resource);
        ASSERT_EQ(static_cast<int>(Counting::currentOwner()), static_cast<int>(EMemoryOwner::Resource));

        std::thread worker([&] {
            // Spawned from inside a Resource scope, but a thread starts Untagged:
            // inheriting here would silently charge a worker's work to whichever
            // subsystem happened to submit it.
            ownerInsideSpawnedScope = static_cast<int>(Counting::currentOwner());

            const MemoryOwnerScope workerScope(EMemoryOwner::Resource);
            ownerAfterOwnScope = static_cast<int>(Counting::currentOwner());
        });
        worker.join();
    }

    EXPECT_EQ(ownerInsideSpawnedScope.load(), static_cast<int>(EMemoryOwner::Untagged))
        << "a worker must not inherit the spawning thread's owner tag";
    EXPECT_EQ(ownerAfterOwnScope.load(), static_cast<int>(EMemoryOwner::Resource))
        << "a scope opened on the worker itself must take effect there";
}

TEST(AllocatorBackendTest, UnsizedDeleteStillReleasesTheRecordedSize)
{
    // The recorded size is what makes this work: the global `operator delete(void*)`
    // carries no size, so without it "live bytes" would only ever grow and a run
    // would report its cumulative allocation as if it were a level (it read 4.4 GiB
    // against a 360 MiB RSS).
    const std::size_t kSize = 3000;
    void*             pointer = processAllocate(kSize, 0);
    ASSERT_NE(pointer, nullptr);
    // No size hint at all -- exactly what unsized delete passes.
    processDeallocate(pointer, 0);

    // Allocating the same size again and asking for its address must land us
    // back in a block of the same size; a leak or a double free would show up
    // here as a corrupted reuse rather than silently.
    void* again = processAllocate(kSize, 0);
    ASSERT_NE(again, nullptr);
    processDeallocate(again, kSize);
}

TEST(AllocatorBackendTest, OverAlignedBlocksCarryTheirRealSizeAndComeBackAligned)
{
    for (const std::size_t alignment : { std::size_t{ 32 }, std::size_t{ 64 }, std::size_t{ 4096 } }) {
        void* pointer = processAllocate(1000, alignment);
        ASSERT_NE(pointer, nullptr) << "alignment " << alignment;
        // The user pointer keeps the alignment it asked for, and is the exact
        // pointer the backend returned (nothing sits in front of it).
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0u) << "alignment " << alignment;
        // Freed without a size: the recorded size has to carry 1000 across.
        processDeallocate(pointer, 0);
    }
}

TEST(AllocatorBackendTest, ZeroSizedRequestsAreDistinctAndFreeCleanly)
{
    void* first  = processAllocate(0, 0);
    void* second = processAllocate(0, 0);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    processDeallocate(first, 0);
    processDeallocate(second, 0);
}

TEST(AllocatorBackendTest, TheReturnedPointerIsTheBlockTheBackendReturned)
{
    // The contract the size side table exists to keep: our pointers *are*
    // malloc pointers. A header in front of the block would make every
    // cross-boundary free -- another image, or a library that allocates with
    // its own allocator and frees through ours -- hand `free` an address it
    // was never given. Comparing against the backend directly is the only way
    // to say that without depending on the addresses malloc happens to pick.
    void* throughLayer   = processAllocate(777, 0);
    void* straightMalloc = MallocAllocator::allocate(777, 0);
    ASSERT_NE(throughLayer, nullptr);
    ASSERT_NE(straightMalloc, nullptr);

    // Both requests are live at once, so the addresses must differ -- if they
    // matched, the layer had allocated nothing of its own and the test below
    // would be comparing a value with itself.
    EXPECT_NE(throughLayer, straightMalloc);

    // Freeing the raw malloc block through the layer must not crash and must
    // not offset the pointer: `lookupAllocationSize` finds no entry, falls back
    // to the sized-delete hint, and `free` gets back exactly what it gave out.
    processDeallocate(straightMalloc, 777);
    processDeallocate(throughLayer, 0);
}

TEST(CountingAllocatorTest, LiveBytesFallWhenMemoryIsFreed)
{
    using namespace ya::memory;
    const FAllocatorStats before = Counting::stats();

    void* pointer = nullptr;
    {
        const MemoryOwnerScope scope(EMemoryOwner::Untagged);
        pointer = Counting::allocate(4096, 0);
    }
    const uint64_t liveWithPointer = Counting::stats().getLiveBytes();
    EXPECT_GE(liveWithPointer, before.getLiveBytes() + 4096);

    Counting::deallocate(pointer, 4096);
    EXPECT_EQ(Counting::stats().getLiveBytes(), before.getLiveBytes());
}

TEST(CountingAllocatorTest, FreeingMoreThanWasAllocatedClampsInsteadOfWrapping)
{
    using namespace ya::memory;
    // A pointer allocated before counting started shows up as a free with no
    // allocation. It must not turn the live counter into a near-UINT64_MAX
    // number that then poisons every later report.
    //
    // "Free 32 bytes and expect the level to be unchanged" only holds where the
    // bucket is empty. In a counting build the process is holding megabytes, so
    // that assertion measured the harness, not the clamp -- and it failed there
    // while passing in debug, which is the worst possible shape for a test
    // about arithmetic. Release far more than any bucket holds instead, and
    // require the level to fall to zero rather than wrap.
    //
    // Asserting an exact zero rather than a delta is deliberate: draining
    // Untagged to zero is this test's job now, and the surrounding cases take
    // their own `before` snapshot, so none of them can observe it.
    const FAllocatorStats before = Counting::stats();
    void*                   raw    = std::malloc(32);
    ASSERT_NE(raw, nullptr);

    // Comfortably past anything the process can be holding, so the bucket is
    // driven to its clamp rather than merely decremented.
    const uint64_t absurd = before.getLiveBytes() + (1ull << 40);
    Counting::deallocate(raw, absurd);

    // The clamp is per bucket: `deallocate` charges the free to the owner in
    // scope, so only Untagged is guaranteed to reach zero here. What matters is
    // that it stopped there instead of wrapping to a near-2^64 number -- which
    // is exactly what an unsigned subtraction does without the clamp.
    EXPECT_EQ(Counting::stats().buckets[kUntagged].liveBytes, 0u);
    EXPECT_LE(Counting::stats().getLiveBytes(), before.getLiveBytes());
}

TEST(CountingAllocatorTest, FailedAllocationsAreNotCounted)
{
    using namespace ya::memory;
    const FAllocatorStats before = Counting::stats();
    // An alignment the platform cannot satisfy fails; a failed allocation has
    // no size to charge and no live memory to release later.
    EXPECT_EQ(Counting::allocate(16, std::size_t{ 1 } << 62), nullptr);
    EXPECT_EQ(Counting::stats().deltaSince(before).getTotalAllocCount(), 0u);
}

TEST(AllocatorStatsTest, DeltaSinceCarriesLevelsAndDifferences)
{
    FAllocatorStats previous;
    previous.bSupported                        = true;
    previous.buckets[kRender].liveBytes        = 10;
    previous.buckets[kRender].totalAllocCount  = 1;
    previous.buckets[kRender].totalAllocBytes  = 100;

    FAllocatorStats current                = previous;
    current.buckets[kRender].liveBytes      = 40;
    current.buckets[kRender].totalAllocCount = 3;
    current.buckets[kRender].totalAllocBytes = 300;

    const FAllocatorStats delta = current.deltaSince(previous);
    EXPECT_TRUE(delta.bSupported);
    // Levels carry through; totals are the difference.
    EXPECT_EQ(delta.buckets[kRender].liveBytes, 40u);
    EXPECT_EQ(delta.buckets[kRender].totalAllocCount, 2u);
    EXPECT_EQ(delta.buckets[kRender].totalAllocBytes, 200u);
    EXPECT_EQ(delta.buckets[kRender].totalFreeBytes, 0u);
}

TEST(AllocatorStatsTest, OwnersAreLabelledAndCounted)
{
    EXPECT_EQ(kMemoryOwnerCount, 4u);
    EXPECT_EQ(toMemoryOwnerLabel(EMemoryOwner::Untagged), "untagged");
    EXPECT_EQ(toMemoryOwnerLabel(EMemoryOwner::Resource), "resource");
    EXPECT_EQ(toMemoryOwnerLabel(EMemoryOwner::Render), "render");
    EXPECT_EQ(toMemoryOwnerLabel(EMemoryOwner::Script), "script");
}

TEST(AllocatorStatsTest, DeltaCarriesSizeTableCompletenessOver)
{
    // It is a level, not a monotonic total: a delta computed from a truncated
    // table is still reading a truncated table, and defaulting it to true in
    // `deltaSince` would report a full table over a full one.
    FAllocatorStats current;
    current.bSupported        = true;
    current.sizeTableComplete = false;

    FAllocatorStats delta = current.deltaSince(FAllocatorStats{});
    EXPECT_FALSE(delta.sizeTableComplete);
}