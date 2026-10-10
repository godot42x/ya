#include "GameEditor/UI/Sections/RuntimeMemorySection.h"

#include "Core/Memory/Allocator.h"
#include "Core/Os/OsMemory.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameRuntime/App.h"
#include "RHI/Core/GpuMemoryStats.h"
#include "RHI/Render.h"

#include <format>
#include <string>

namespace ya
{
namespace
{

void setRow(const FStatReadout& row, std::string text)
{
    row.value->setText(text);
    row.root->setVisibility(EWidgetVisibility::Visible);
}

void hideRow(const FStatReadout& row)
{
    row.root->setVisibility(EWidgetVisibility::Collapsed);
}

/// "24.4 MiB (19)" -- the size a reader compares, with the allocation count
/// that says whether it is a few big objects or many small ones.
std::string formatSizedCount(uint64_t bytes, uint32_t count)
{
    return std::format("{} ({})", formatMiB(bytes), count);
}

} // namespace

RuntimeMemorySection::RuntimeMemorySection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeMemorySection::construct()
{
    _gpuTotal = makeStatRow("RuntimeMemoryGpuTotal", "GPU");
    for (uint32_t i = 0; i < _gpuClasses.size(); ++i) {
        _gpuClasses[i] = makeStatRow(std::format("RuntimeMemoryClass{}", i),
                                     toGpuMemoryClassLabel(kGpuMemoryClasses[i]));
    }
    for (uint32_t i = 0; i < _gpuHeaps.size(); ++i) {
        _gpuHeaps[i] = makeStatRow(std::format("RuntimeMemoryHeap{}", i), std::format("heap {}", i));
    }
    _rss  = makeStatRow("RuntimeMemoryRss", "RSS");
    _peak = makeStatRow("RuntimeMemoryPeak", "Peak");
    _heap = makeStatRow("RuntimeMemoryHeapTotal", "Heap");
    for (uint32_t i = 0; i < memory::kMemoryOwnerCount; ++i) {
        _heapOwners[i] = makeStatRow(std::format("RuntimeMemoryOwner{}", i),
                                     memory::toMemoryOwnerLabel(static_cast<memory::EMemoryOwner>(i)));
    }
    _caveat = ui::text("RuntimeMemoryCaveat").setStyleKey("text.muted").share();

    auto rows = ui::column("RuntimeMemoryRows").setSpacing(editor_density::kRowSpacing);
    rows.child(_gpuTotal.root);
    for (const FStatReadout& row : _gpuClasses) {
        rows.child(row.root);
    }
    for (const FStatReadout& row : _gpuHeaps) {
        rows.child(row.root);
    }
    rows.child(_rss.root);
    rows.child(_peak.root);
    rows.child(_heap.root);
    for (const FStatReadout& row : _heapOwners) {
        rows.child(row.root);
    }
    rows.child(_caveat);
    addDetachedChild(rows.release());
}

void RuntimeMemorySection::sync(const App* app)
{
    // "Cannot answer", not "holds nothing": a row of zeros would read as a
    // measurement on a backend or platform that has no counter.
    const IRender* render = app ? app->getRenderServices().getRender() : nullptr;
    const FGpuMemoryStats gpuStats = render ? render->getGpuMemoryStats() : FGpuMemoryStats{};
    _readout.gpuSupported   = gpuStats.bSupported;
    _readout.gpuTotalBytes  = gpuStats.getTotalBytes();
    _readout.gpuAllocations = gpuStats.getTotalAllocationCount();
    if (!gpuStats.bSupported) {
        _gpuTotal.value->setText("n/a");
        for (const FStatReadout& row : _gpuClasses) {
            hideRow(row);
        }
        for (const FStatReadout& row : _gpuHeaps) {
            hideRow(row);
        }
    }
    else {
        setRow(_gpuTotal, formatSizedCount(gpuStats.getTotalBytes(), gpuStats.getTotalAllocationCount()));
        for (uint32_t i = 0; i < _gpuClasses.size(); ++i) {
            const FGpuMemoryClassUsage& usage = gpuStats.byClass[i];
            if (usage.allocationCount == 0) {
                hideRow(_gpuClasses[i]);
            }
            else {
                setRow(_gpuClasses[i], formatSizedCount(usage.bytes, usage.allocationCount));
            }
        }
        for (uint32_t i = 0; i < _gpuHeaps.size(); ++i) {
            if (i >= gpuStats.heaps.size()) {
                hideRow(_gpuHeaps[i]);
                continue;
            }
            const FGpuMemoryHeapBudget& heap = gpuStats.heaps[i];
            setRow(_gpuHeaps[i], std::format("{} / {}", formatMiB(heap.usageBytes), formatMiB(heap.budgetBytes)));
        }
    }

    const Os::FProcessMemoryStats processStats = Os::sampleProcessMemory();
    _readout.processSupported = processStats.bSupported;
    _readout.rssBytes         = processStats.residentBytes;
    _readout.peakBytes        = processStats.peakResidentBytes;
    setRow(_rss, processStats.bSupported ? formatMiB(processStats.residentBytes) : std::string("n/a"));
    setRow(_peak, processStats.bSupported ? formatMiB(processStats.peakResidentBytes) : std::string("n/a"));

    // Allocator layer, when this build counts. "not counted" rather than zeros:
    // a debug or release build has no counting allocator compiled in.
    const memory::FAllocatorStats allocator = memory::allocatorStatsSnapshot();
    if (!allocator.bSupported) {
        _heap.value->setText("not counted in this build");
        for (const FStatReadout& row : _heapOwners) {
            hideRow(row);
        }
    }
    else {
        // A full size table means the byte level is real; once it is not, the
        // level is a floor and saying so beats printing a confident number.
        setRow(_heap,
               formatSizedCount(allocator.getLiveBytes(), allocator.getLiveAllocations()) +
                   (allocator.sizeTableComplete ? "" : " (size table full)"));
        for (uint32_t owner = 0; owner < memory::kMemoryOwnerCount; ++owner) {
            const memory::FAllocatorBucketStats& bucket = allocator.buckets[owner];
            if (bucket.liveBytes == 0) {
                hideRow(_heapOwners[owner]);
            }
            else {
                setRow(_heapOwners[owner], formatMiB(bucket.liveBytes));
            }
        }
    }

    // The one line that keeps the two rows from being read as one total.
    _caveat->setText(Os::hasUnifiedMemory()
                         ? "Unified memory: GPU and process memory share physical pages -- do not add them."
                         : "GPU and process memory are separate pools.");
}

} // namespace ya
