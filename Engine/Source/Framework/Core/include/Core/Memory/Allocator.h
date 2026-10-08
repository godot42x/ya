#pragma once

#include "Core/Memory/AllocatorStats.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

// ============================================================================
// The engine's allocator layer.
//
// Every module's replaced global `operator new` / `operator delete` forwards
// here (`processAllocate` / `processDeallocate`), so "which allocator is the
// process using" is one place in the engine rather than a property of the
// platform.
//
// Dispatch is static, not virtual. This is one level below `operator new` --
// the hottest call in the process -- and the allocator is a build-time
// decision anyway: swapping in mimalloc or jemalloc means reconfiguring and
// relinking, not flipping a pointer at runtime. (Flipping a pointer at
// runtime is not merely slower, it is wrong: pointers the previous allocator
// handed out would be freed by the new one.)
//
// The build-time choice lives at the bottom of this header; the rest is the
// contract every backend satisfies and the two that exist today.
// ============================================================================

namespace ya::memory
{

/// The static contract every allocator backend satisfies. A concept rather
/// than a base class: there is nothing to gain from a vtable here and one
/// call of it per allocation to lose.
template <typename B>
concept AllocatorBackend = requires(std::size_t size, std::size_t alignment, void* pointer) {
    { B::allocate(size, alignment) } -> std::same_as<void*>;
    { B::deallocate(pointer, size) } noexcept;
    { B::name() } -> std::convertible_to<std::string_view>;
    { B::stats() } -> std::same_as<FAllocatorStats>;
};

/// `std::malloc` / `std::free`. The default process allocator, and the
/// backend the counting allocator measures on top of.
struct MallocAllocator
{
    /// `size` may be zero (a distinct, valid, non-null result is required).
    /// `alignment` is zero for the platform default or a power of two.
    /// Returns nullptr when the request cannot be satisfied; the caller
    /// decides whether that is `std::bad_alloc` or a calmer answer.
    [[nodiscard]] static void* allocate(std::size_t size, std::size_t alignment);

    /// `pointer` may be null (no-op). `size` is the sized-delete hint: it may
    /// be zero or wrong, and a backend must not depend on it being either.
    static void deallocate(void* pointer, std::size_t size) noexcept;

    static constexpr std::string_view name() { return "malloc"; }

    /// Plain malloc counts nothing, and says so rather than reporting zeros as
    /// if it did.
    [[nodiscard]] static FAllocatorStats stats() { return {}; }
};

/// A backend that counts what passes through it, per owner bucket, and
/// forwards the memory work to `Backend`.
///
/// Static members, because a backend has process scope by construction. They
/// are updated in one place -- `processAllocate` lives in Core and every
/// module routes through it -- so there is exactly one set of counters per
/// instantiation; a module calling `CountingAllocator::allocate` directly
/// would get its own copy and silently stop counting for the process.
template <AllocatorBackend Backend>
struct CountingAllocator
{
    [[nodiscard]] static void* allocate(std::size_t size, std::size_t alignment)
    {
        void* pointer = Backend::allocate(size, alignment);
        if (pointer == nullptr) {
            return nullptr;
        }
        const uint32_t owner = static_cast<uint32_t>(currentOwner());
        // Relaxed: these are counters, not synchronization. Nothing waits on
        // them, and each atomic is independent of the others.
        s_liveBytes[owner].fetch_add(size, std::memory_order_relaxed);
        s_liveAllocations[owner].fetch_add(1, std::memory_order_relaxed);
        s_totalAllocCount[owner].fetch_add(1, std::memory_order_relaxed);
        s_totalAllocBytes[owner].fetch_add(size, std::memory_order_relaxed);
        return pointer;
    }

    static void deallocate(void* pointer, std::size_t size) noexcept
    {
        if (pointer == nullptr) {
            return;
        }
        const uint32_t owner = static_cast<uint32_t>(currentOwner());
        // The bucket is the one current at free time, which is not necessarily
        // the one current at allocation time. Attributing a free to the current
        // scope is the only thing a thread-local tag can honestly do, so
        // `liveBytes` reads as "bytes allocated under this tag and not yet
        // freed from it". Clamped rather than wrapping: a pointer allocated
        // before counting started must not turn the report into a huge number.
        for (uint64_t current = s_liveBytes[owner].load(std::memory_order_relaxed); current != 0;) {
            const uint64_t next = current > size ? current - size : 0;
            if (s_liveBytes[owner].compare_exchange_weak(current, next, std::memory_order_relaxed)) {
                break;
            }
        }
        for (uint64_t current = s_liveAllocations[owner].load(std::memory_order_relaxed); current != 0;) {
            if (s_liveAllocations[owner].compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
                break;
            }
        }
        s_totalFreeCount[owner].fetch_add(1, std::memory_order_relaxed);
        s_totalFreeBytes[owner].fetch_add(size, std::memory_order_relaxed);
        Backend::deallocate(pointer, size);
    }

    static constexpr std::string_view name() { return "counting"; }

    /// The thread's current bucket. Public because the counting is baked into
    /// the build, not installed at runtime; engine code charges buckets
    /// through `MemoryOwnerScope`.
    [[nodiscard]] static EMemoryOwner currentOwner();

    [[nodiscard]] static FAllocatorStats stats();

  private:
    /// Zero-initialized on purpose: the process allocator runs before any
    /// dynamic initialization is guaranteed, so these must not depend on it.
    /// `std::atomic<uint64_t>{}` is constant-initialized in C++20.
    inline static std::atomic<uint64_t> s_liveBytes[kMemoryOwnerCount]{};
    inline static std::atomic<uint64_t> s_liveAllocations[kMemoryOwnerCount]{};
    inline static std::atomic<uint64_t> s_totalAllocCount[kMemoryOwnerCount]{};
    inline static std::atomic<uint64_t> s_totalAllocBytes[kMemoryOwnerCount]{};
    inline static std::atomic<uint64_t> s_totalFreeCount[kMemoryOwnerCount]{};
    inline static std::atomic<uint64_t> s_totalFreeBytes[kMemoryOwnerCount]{};
};

/// Which allocator the process's global functions route through.
///
/// Chosen here, in C++, from the build mode rather than from the build script:
/// xmake evaluates the project description before the mode option is loaded, so
/// a query there answers for whichever mode was configured, not the one being
/// compiled. Only the counting backend is mode-dependent -- it is a measurement,
/// and a measurement belongs to the build that measures. Everything else, in
/// every build, routes through this one layer.
///
/// Switching backend (mimalloc, jemalloc, ...) is this alias plus a dependency.
#ifdef YA_BUILD_MODE_PROFILE
using ProcessAllocator = CountingAllocator<MallocAllocator>;
#else
using ProcessAllocator = MallocAllocator;
#endif

/// Route one allocation through the process allocator. Public because every
/// module compiles its own copy of the global allocation functions and each
/// copy forwards here (Mach-O binds an image's undefined `operator new` to
/// that same image, and MSVC gives every DLL its own CRT, so a single copy
/// anywhere else would only ever route one module's allocations).
[[nodiscard]] YA_CORE_API void* processAllocate(std::size_t size, std::size_t alignment = 0);

/// The matching deallocation. `noexcept`: a destructor path may not throw.
///
/// `size` is the sized-delete hint and is treated as a hint: the layer keeps the
/// real size in a header written at allocation time, because most calls through
/// the global `operator delete` carry no size at all, and a level that only ever
/// grows is not a level. The header is what makes "live bytes" mean anything.
YA_CORE_API void processDeallocate(void* pointer, std::size_t size) noexcept;

/// True when the process allocator is one that counts. Compile-time: the
/// answer is a property of the build, and the report prints "n/a" when false
/// instead of a row of zeros.
[[nodiscard]] constexpr bool processAllocatorCounts()
{
#ifdef YA_BUILD_MODE_PROFILE
    return true;
#else
    return false;
#endif
}

/// Counters of the process allocator. Unsupported (all-zero, `bSupported`
/// false) in builds that do not count -- the caller shows "n/a", never a
/// measurement of nothing.
[[nodiscard]] YA_CORE_API FAllocatorStats allocatorStatsSnapshot();

} // namespace ya::memory
