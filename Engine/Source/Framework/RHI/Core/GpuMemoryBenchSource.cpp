#include "RHI/Core/GpuMemoryBenchSource.h"

#include <algorithm>
#include <string>

namespace ya
{

namespace
{

constexpr double kBytesPerMiB = 1024.0 * 1024.0;

[[nodiscard]] double toMiB(uint64_t bytes)
{
    return static_cast<double>(bytes) / kBytesPerMiB;
}

} // namespace

GpuMemoryBenchSource::GpuMemoryBenchSource(StatsProvider provider)
    : _provider(std::move(provider))
{
}

void GpuMemoryBenchSource::onWindowBegin()
{
    _peakBytes.fill(0);
    _heaps.clear();
    _bSupported = false;
}

void GpuMemoryBenchSource::sampleFrame(profiling::BenchSeriesWriter& out)
{
    if (!_provider) {
        return;
    }

    const FGpuMemoryStats stats = _provider();
    _bSupported                 = stats.bSupported;
    _heaps                      = stats.heaps;

    uint64_t totalBytes = 0;
    for (const EGpuMemoryClass::T memoryClass : kGpuMemoryClasses) {
        const uint32_t slot       = gpuMemoryClassIndex(memoryClass);
        const uint64_t classBytes = stats.byClass[slot].bytes;
        totalBytes += classBytes;
        _peakBytes[slot] = std::max(_peakBytes[slot], classBytes);
        out.record(std::string(toGpuMemoryClassLabel(memoryClass)) + "MiB", static_cast<float>(toMiB(classBytes)));
    }
    out.record("totalMiB", static_cast<float>(toMiB(totalBytes)));
}

void GpuMemoryBenchSource::writeFacts(nlohmann::json& section) const
{
    // A backend that cannot answer would otherwise read as a real measurement
    // of "everything is zero".
    section["supported"] = _bSupported;

    for (const EGpuMemoryClass::T memoryClass : kGpuMemoryClasses) {
        section[std::string("peak.") + std::string(toGpuMemoryClassLabel(memoryClass)) + "MiB"] =
            toMiB(_peakBytes[gpuMemoryClassIndex(memoryClass)]);
    }

    nlohmann::json heaps = nlohmann::json::array();
    for (const FGpuMemoryHeapBudget& heap : _heaps) {
        heaps.push_back({
            {"heapIndex",     heap.heapIndex},
            {"budgetMiB",     toMiB(heap.budgetBytes)},
            {"usageMiB",      toMiB(heap.usageBytes)},
            {"allocationMiB", toMiB(heap.allocationBytes)},
            {"blockMiB",      toMiB(heap.blockBytes)},
        });
    }
    section["heaps"] = std::move(heaps);
}

} // namespace ya
