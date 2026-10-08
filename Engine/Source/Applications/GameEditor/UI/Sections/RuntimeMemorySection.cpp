#include "GameEditor/UI/Sections/RuntimeMemorySection.h"

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

constexpr double kBytesPerMiB = 1024.0 * 1024.0;

/// A number this panel prints is a size a reader compares against another
/// size, so it keeps one decimal place below 100 MiB and none above -- a row
/// of "12.3 / 45.6 / 789.0 MiB" is easier to scan than one of "12.34 / ...".
std::string formatMiB(uint64_t bytes)
{
    const double mib = static_cast<double>(bytes) / kBytesPerMiB;
    return mib < 100.0 ? std::format("{:.1f} MiB", mib) : std::format("{:.0f} MiB", mib);
}

std::string joinRow(std::string row, std::string_view part)
{
    if (!row.empty()) {
        row += "  ";
    }
    row += part;
    return row;
}

} // namespace

RuntimeMemorySection::RuntimeMemorySection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeMemorySection::construct()
{
    _gpuTotal   = ui::text("RuntimeMemoryGpuTotal").share();
    _gpuClasses = ui::text("RuntimeMemoryGpuClasses").share();
    _gpuHeaps   = ui::text("RuntimeMemoryGpuHeaps").share();
    _process    = ui::text("RuntimeMemoryProcess").share();
    _caveat     = ui::text("RuntimeMemoryCaveat").setStyleKey("text.muted").share();

    addDetachedChild(ui::column("RuntimeMemoryRows")
                         .setSpacing(4.0f)
                         .child(ui::text("RuntimeMemoryHeader").setText("Memory").setStyleKey("text.header"))
                         .child(_gpuTotal)
                         .child(_gpuClasses)
                         .child(_gpuHeaps)
                         .child(_process)
                         .child(_caveat)
                         .release());
}

void RuntimeMemorySection::sync(const App* app)
{
    if (!_gpuTotal || !_gpuClasses || !_gpuHeaps || !_process || !_caveat) {
        return;
    }

    // "Cannot answer", not "holds nothing": a row of zeros would read as a
    // measurement on a backend or platform that has no counter.
    const IRender* render = app ? app->getRenderServices().getRender() : nullptr;
    const FGpuMemoryStats gpuStats = render ? render->getGpuMemoryStats() : FGpuMemoryStats{};
    if (!gpuStats.bSupported) {
        _gpuTotal->setText("GPU memory: n/a");
        _gpuClasses->setText("");
        _gpuHeaps->setText("");
    }
    else {
        _gpuTotal->setText(std::format("GPU memory: {} in {} allocations",
                                       formatMiB(gpuStats.getTotalBytes()),
                                       gpuStats.getTotalAllocationCount()));

        std::string classes;
        for (const EGpuMemoryClass::T memoryClass : kGpuMemoryClasses) {
            const FGpuMemoryClassUsage& usage = gpuStats.byClass[gpuMemoryClassIndex(memoryClass)];
            if (usage.allocationCount == 0) {
                continue;
            }
            classes = joinRow(std::move(classes),
                              std::format("{} {} ({})",
                                          toGpuMemoryClassLabel(memoryClass),
                                          formatMiB(usage.bytes),
                                          usage.allocationCount));
        }
        _gpuClasses->setText(classes.empty() ? "no allocations" : classes);

        std::string heaps;
        for (const FGpuMemoryHeapBudget& heap : gpuStats.heaps) {
            heaps = joinRow(std::move(heaps),
                            std::format("heap {}: {} / {}",
                                        heap.heapIndex,
                                        formatMiB(heap.usageBytes),
                                        formatMiB(heap.budgetBytes)));
        }
        _gpuHeaps->setText(heaps.empty() ? "no memory heaps reported" : heaps);
    }

    const Os::FProcessMemoryStats processStats = Os::sampleProcessMemory();
    if (!processStats.bSupported) {
        _process->setText("Process memory: n/a");
    }
    else {
        _process->setText(std::format("Process memory: RSS {}, peak {}",
                                      formatMiB(processStats.residentBytes),
                                      formatMiB(processStats.peakResidentBytes)));
    }

    // The one line that keeps the two rows from being read as one total.
    _caveat->setText(Os::hasUnifiedMemory()
                         ? "Unified memory: GPU and process memory share physical pages -- do not add them."
                         : "GPU and process memory are separate pools.");
}

} // namespace ya
