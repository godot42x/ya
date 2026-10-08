#pragma once

#include "Core/Api.h"

#include <cstdint>

namespace ya::Os
{

/// What the OS says this process currently holds, in bytes.
///
/// This is the *process* half of the memory report and it is deliberately a
/// different kind of number from `IRender::getGpuMemoryStats()`: that one is
/// what the GPU allocator was handed, this one is what the OS charged the whole
/// process -- including the allocator's own bookkeeping, the CPU-side copies of
/// every texture, the script VM and the editor. Apple Silicon shares one pool
/// of physical memory between the two, so they overlap and must never be added
/// together; the report lists them side by side and flags that with
/// `hasUnifiedMemory()`.
struct FProcessMemoryStats
{
    /// False when the platform has no counter to read. Callers show "n/a"
    /// rather than a row of zeros.
    bool     bSupported         = false;
    /// Resident set size: physical memory this process has mapped right now.
    /// Not "how much it allocated" -- a page the allocator freed but the OS has
    /// not reclaimed yet still counts.
    uint64_t residentBytes      = 0;
    /// High-water mark of `residentBytes` since the process started. The OS
    /// keeps it, so a window that opens after a load spike still sees the spike.
    uint64_t peakResidentBytes  = 0;
};

/// Sample this process' memory. Cheap enough to call once a frame (one syscall
/// and, on Linux, two small file reads).
[[nodiscard]] YA_CORE_API FProcessMemoryStats sampleProcessMemory();

/// Whether the CPU and the GPU draw from the same physical memory on this
/// machine: true on Apple Silicon, false where the GPU has its own VRAM.
///
/// Declared rather than a compile-time constant in the header because "is the
/// GPU's memory also the process' memory" is a machine fact that a later
/// backend may want to answer from the device instead.
[[nodiscard]] YA_CORE_API bool hasUnifiedMemory();

} // namespace ya::Os
