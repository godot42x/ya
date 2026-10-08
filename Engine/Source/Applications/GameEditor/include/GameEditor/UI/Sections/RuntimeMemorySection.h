#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{
struct App;
struct UIText;

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

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _gpuTotal;
    std::shared_ptr<UIText> _gpuClasses;
    std::shared_ptr<UIText> _gpuHeaps;
    std::shared_ptr<UIText> _process;
    std::shared_ptr<UIText> _heap;
    std::shared_ptr<UIText> _caveat;
};
} // namespace ya
