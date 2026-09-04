#pragma once

#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct WidgetTree;

/// Legacy ImGui offscreen workbench adapter. ImGui render and compositor path
/// removed in Phase 8H; retained EditorSurface `FWorkbenchSurface` is the only
/// GUI Workbench UI. This type remains compiled until the file is deleted.
struct GUIWorkbenchPanel
{
  private:
    EditorLayer* _owner = nullptr;
    guiworkbench::FWorkbenchSurface _surface;
    std::unique_ptr<WidgetTree>     _tree;
    Extent2D                        _logicalExtent{};

  public:
    explicit GUIWorkbenchPanel(EditorLayer* owner);

    [[nodiscard]] bool             hasRenderableExtent() const { return _logicalExtent.width > 0 && _logicalExtent.height > 0; }
    [[nodiscard]] Extent2D         getLogicalExtent() const { return _logicalExtent; }
    [[nodiscard]] UIFrameSnapshot buildSnapshot();

  private:
    void ensureTree();
};

} // namespace ya
