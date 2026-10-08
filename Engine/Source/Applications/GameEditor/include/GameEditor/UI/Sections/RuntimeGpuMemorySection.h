#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{
struct App;
struct UIText;

/// Runtime Tools section: what the render device's GPU memory is spent on.
///
/// A separate section from the profiling one because it is a different kind of
/// number -- bytes held right now, not time spent per frame -- and it comes
/// from the device (`IRender::getGpuMemoryStats`) rather than from `PerfState`.
/// The same figures go into the `gpu_memory` section of a benchmark summary.
class RuntimeGpuMemorySection final : public UICompoundWidget
{
  public:
    explicit RuntimeGpuMemorySection(std::string name = "RuntimeGpuMemory");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _total;
    std::shared_ptr<UIText> _classes;
    std::shared_ptr<UIText> _heaps;
};
} // namespace ya
