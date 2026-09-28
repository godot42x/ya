#pragma once

#include "GUI/Binding/Reactive.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
class UICanvasSlot;
struct WidgetTree;

/// The project selector as a self-contained page. The launch flow hosts it in
/// its own window before any editor chrome exists; the page owns widgets,
/// filtering and selection, while EditorLayer stays the fact source
/// (discovered projects, selection, browser error).
class EditorProjectBrowserPage
{
  public:
    /// Raised on Open Project with the discovered path. The launch flow owns
    /// the window handoff (close browser -> splash -> editor window).
    std::function<void(const std::string& projectPath)> onOpenRequested;

    void build(EditorLayer& layer, WidgetTree& tree);
    /// Per-frame from the host delegate: adaptive card clamp against the
    /// window extent plus a row flush when something marked them dirty.
    void tick();

  private:
    void markRowsDirty();
    void flushRows();
    void selectRow(const std::string& rowId);

    EditorLayer*                                     _layer = nullptr;
    WidgetTree*                                      _tree  = nullptr;
    std::shared_ptr<UITreeView>                      _list;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _roots;
    std::shared_ptr<UIText>                          _errorText;
    std::shared_ptr<UIText>                          _pathText;
    std::shared_ptr<SelectionModel>                  _selection;
    UICanvasSlot*                                    _cardSlot = nullptr;
    /// Case-insensitive substring filter on the project file name and path.
    std::string      _filter;
    /// Filtered row -> index into EditorLayer::getDiscoveredProjects().
    std::vector<int> _rowToProject;
    bool             _bRowsDirty = false;
};

} // namespace ya
