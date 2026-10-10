#pragma once

#include "Core/Memory/AllocatorStats.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/Shell/EditorStatReadouts.h"

#include <array>
#include <cstdint>
#include <memory>

namespace ya
{
struct App;
struct UIText;

/// The memory numbers the KPI strip pins, read once per frame by this section.
/// The strip owns no sampling of its own: it reads this, so the strip and the
/// detail rows below it can never report two different totals for one frame.
struct FRuntimeMemoryReadout
{
    bool     gpuSupported     = false;
    uint64_t gpuTotalBytes    = 0;
    uint32_t gpuAllocations   = 0;
    bool     processSupported = false;
    uint64_t rssBytes         = 0;
    uint64_t peakBytes        = 0;
};

/// Runtime Tools section: what the running process holds, in the two
/// measures the memory report keeps apart -- GPU memory (what the render
/// device's allocator was handed) and process memory (what the OS charged the
/// whole process).
///
/// The two are never added together: on unified-memory machines the same
/// physical pages can be counted by both, which the section says out loud.
/// The same two figures go into the `gpu_memory` and `process_memory`
/// sections of a benchmark summary.
class RuntimeMemorySection final : public UICompoundWidget
{
  public:
    explicit RuntimeMemorySection(std::string name = "RuntimeMemory");
    void sync(const App* app);

    [[nodiscard]] const FRuntimeMemoryReadout& readout() const { return _readout; }

  protected:
    void construct() override;

  private:
    FRuntimeMemoryReadout _readout;

    FStatReadout _gpuTotal;
    /// One row per GPU memory class; a class with no live allocation is
    /// collapsed rather than shown as a zero.
    std::array<FStatReadout, 5> _gpuClasses;
    /// Budget rows, pooled: the device reports one heap on the machines this
    /// runs on, but the field is a list and the panel must not assume a count.
    std::array<FStatReadout, 4> _gpuHeaps;
    FStatReadout _rss;
    FStatReadout _peak;
    FStatReadout _heap;
    /// One row per allocator owner bucket (Untagged / Resource / Render /
    /// Script); nonzero buckets only, so the panel does not print four zeros.
    std::array<FStatReadout, memory::kMemoryOwnerCount> _heapOwners;
    std::shared_ptr<UIText> _caveat;
};
} // namespace ya
