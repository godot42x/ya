#pragma once

#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/GpuMemoryClass.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace ya
{

/// The bucket an image lands in when its creator left `memoryClass` on `Auto`.
/// An image rendered into is a render target -- transient when the usage says
/// its content need not outlive the pass -- and anything else is sampled
/// content.
[[nodiscard]] inline constexpr EGpuMemoryClass::T resolveGpuMemoryClass(EGpuMemoryClass::T requested, EImageUsage::T usage)
{
    if (requested != EGpuMemoryClass::Auto) {
        return requested;
    }
    if ((usage & EImageUsage::TransientAttachment) != 0) {
        return EGpuMemoryClass::Transient;
    }
    if ((usage & (EImageUsage::ColorAttachment | EImageUsage::DepthStencilAttachment)) != 0) {
        return EGpuMemoryClass::RenderTarget;
    }
    return EGpuMemoryClass::Texture;
}

/// The bucket a buffer lands in when its creator left `memoryClass` on `Auto`:
/// host-visible CPU-write memory is the upload region, everything else is
/// device-resident.
[[nodiscard]] inline constexpr EGpuMemoryClass::T resolveGpuMemoryClass(EGpuMemoryClass::T requested, EMemoryUsage memoryUsage)
{
    if (requested != EGpuMemoryClass::Auto) {
        return requested;
    }
    return memoryUsage == EMemoryUsage::CpuToGpu ? EGpuMemoryClass::Upload : EGpuMemoryClass::Buffer;
}

struct FGpuMemoryClassUsage
{
    uint64_t bytes           = 0;
    uint32_t allocationCount = 0;
};

/// One device memory heap as the allocator reports it.
///
/// `budgetBytes` is what the driver says this process may use and `usageBytes`
/// what it currently does -- both cover the whole process, including objects
/// the allocator does not own (swapchain, pipelines, command buffers). They are
/// system numbers only when the device exposes `VK_EXT_memory_budget`; without
/// it the allocator falls back to the heap size and its own block bytes, which
/// is why the two are reported side by side with the allocator's own
/// `allocationBytes`.
struct FGpuMemoryHeapBudget
{
    uint32_t heapIndex = 0;
    uint64_t budgetBytes     = 0;
    uint64_t usageBytes      = 0;
    /// Bytes of live `VmaAllocation` objects in this heap.
    uint64_t allocationBytes = 0;
    /// Bytes of `VkDeviceMemory` blocks this heap holds; the difference to
    /// `allocationBytes` is memory reserved but not currently backing anything.
    uint64_t blockBytes      = 0;
};

/// What one device's GPU memory looks like right now.
struct FGpuMemoryStats
{
    /// False when the backend cannot answer (no allocator yet, or an API with
    /// no such fact). Callers print "n/a" rather than a row of zeros.
    bool                                                  bSupported = false;
    std::array<FGpuMemoryClassUsage, kGpuMemoryClassCount> byClass{};
    std::vector<FGpuMemoryHeapBudget>                      heaps;

    [[nodiscard]] uint64_t getClassBytes(EGpuMemoryClass::T memoryClass) const
    {
        return byClass[gpuMemoryClassIndex(memoryClass)].bytes;
    }

    [[nodiscard]] uint64_t getTotalBytes() const
    {
        uint64_t total = 0;
        for (const FGpuMemoryClassUsage& usage : byClass) {
            total += usage.bytes;
        }
        return total;
    }

    [[nodiscard]] uint64_t getTotalAllocationCount() const
    {
        uint64_t total = 0;
        for (const FGpuMemoryClassUsage& usage : byClass) {
            total += usage.allocationCount;
        }
        return total;
    }
};

/// Running per-class totals a backend keeps next to its allocator: every
/// allocation it hands out is charged here and released when it goes back.
///
/// Resources are created and destroyed from more than one thread (streaming,
/// deferred deletion), so the counters are atomic rather than guarded by a lock
/// the whole device would have to share. `release` clamps at zero instead of
/// wrapping: a leaked or double-counted allocation must not turn the next
/// report into a near-`UINT64_MAX` number.
class GpuMemoryAccount
{
    std::atomic<uint64_t> _bytes[kGpuMemoryClassCount];
    std::atomic<uint32_t> _allocations[kGpuMemoryClassCount];

  public:
    void track(EGpuMemoryClass::T memoryClass, uint64_t bytes)
    {
        // `Auto` would index before the arrays: it is a request to derive the
        // class, never a bucket of its own.
        YA_CORE_ASSERT(memoryClass != EGpuMemoryClass::Auto, "charge a resolved GPU memory class, not Auto");
        const uint32_t slot = gpuMemoryClassIndex(memoryClass);
        _bytes[slot].fetch_add(bytes, std::memory_order_relaxed);
        _allocations[slot].fetch_add(1u, std::memory_order_relaxed);
    }

    void release(EGpuMemoryClass::T memoryClass, uint64_t bytes)
    {
        YA_CORE_ASSERT(memoryClass != EGpuMemoryClass::Auto, "release a resolved GPU memory class, not Auto");
        const uint32_t slot = gpuMemoryClassIndex(memoryClass);
        for (uint64_t current = _bytes[slot].load(std::memory_order_relaxed); current != 0;) {
            const uint64_t next = current > bytes ? current - bytes : 0;
            if (_bytes[slot].compare_exchange_weak(current, next, std::memory_order_relaxed)) {
                break;
            }
        }
        for (uint32_t current = _allocations[slot].load(std::memory_order_relaxed); current != 0;) {
            if (_allocations[slot].compare_exchange_weak(current, current - 1u, std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void fillByClass(std::array<FGpuMemoryClassUsage, kGpuMemoryClassCount>& out) const
    {
        for (uint32_t slot = 0; slot < kGpuMemoryClassCount; ++slot) {
            out[slot].bytes           = _bytes[slot].load(std::memory_order_relaxed);
            out[slot].allocationCount = _allocations[slot].load(std::memory_order_relaxed);
        }
    }
};

} // namespace ya
