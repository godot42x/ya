#include "GameEditor/UI/Sections/RuntimeGpuMemorySection.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
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

} // namespace

RuntimeGpuMemorySection::RuntimeGpuMemorySection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeGpuMemorySection::construct()
{
    _total   = ui::text("RuntimeGpuMemoryTotal").share();
    _classes = ui::text("RuntimeGpuMemoryClasses").share();
    _heaps   = ui::text("RuntimeGpuMemoryHeaps").share();

    addDetachedChild(ui::column("RuntimeGpuMemoryRows")
                         .setSpacing(4.0f)
                         .child(ui::text("RuntimeGpuMemoryHeader").setText("GPU Memory").setStyleKey("text.header"))
                         .child(_total)
                         .child(_classes)
                         .child(_heaps)
                         .release());
}

void RuntimeGpuMemorySection::sync(const App* app)
{
    if (!_total || !_classes || !_heaps) {
        return;
    }

    const IRender* render = app ? app->getRenderServices().getRender() : nullptr;
    if (!render) {
        _total->setText("GPU Memory: n/a");
        _classes->setText("");
        _heaps->setText("");
        return;
    }

    const FGpuMemoryStats stats = render->getGpuMemoryStats();
    if (!stats.bSupported) {
        // "Cannot answer", not "holds nothing": a row of zeros would read as
        // a measurement.
        _total->setText("GPU Memory: n/a");
        _classes->setText("");
        _heaps->setText("");
        return;
    }

    _total->setText(std::format("GPU Memory: {} in {} allocations",
                                formatMiB(stats.getTotalBytes()),
                                stats.getTotalAllocationCount()));

    std::string classes;
    for (const EGpuMemoryClass::T memoryClass : kGpuMemoryClasses) {
        const FGpuMemoryClassUsage& usage = stats.byClass[gpuMemoryClassIndex(memoryClass)];
        if (usage.allocationCount == 0) {
            continue;
        }
        if (!classes.empty()) {
            classes += "  ";
        }
        classes += std::format("{} {} ({})",
                               toGpuMemoryClassLabel(memoryClass),
                               formatMiB(usage.bytes),
                               usage.allocationCount);
    }
    _classes->setText(classes.empty() ? "no allocations" : classes);

    std::string heaps;
    for (const FGpuMemoryHeapBudget& heap : stats.heaps) {
        if (!heaps.empty()) {
            heaps += "  ";
        }
        heaps += std::format("heap {}: {} / {}", heap.heapIndex, formatMiB(heap.usageBytes), formatMiB(heap.budgetBytes));
    }
    _heaps->setText(heaps.empty() ? "no memory heaps reported" : heaps);
}

} // namespace ya
