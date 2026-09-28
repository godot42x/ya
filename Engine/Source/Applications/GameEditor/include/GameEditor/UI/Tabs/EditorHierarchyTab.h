#pragma once

#include "Core/Delegate.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Controls/TreeView.h"

#include <memory>
#include <string>

namespace ya
{

class ActionMap;
struct EditorLayer;
struct SelectionModel;
struct UITextField;

/// Scene hierarchy dock tab. Tree data and selection follow EditorLayer
/// delegates; this widget does not poll fingerprints from the surface.
class EditorHierarchyTab : public UICompoundWidget
{
  public:
    EditorHierarchyTab(EditorLayer& layer, SelectionModel& selection, ActionMap& actions);
    ~EditorHierarchyTab() override;

    void onAttached() override;
    void onDetached() override;

  protected:
    void construct() override;

  private:
    EditorLayer*    _layer     = nullptr;
    SelectionModel* _selection = nullptr;
    ActionMap*      _actions   = nullptr;

    std::shared_ptr<UITreeView>                      _treeView;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _roots;
    std::shared_ptr<Reactive<std::string>>           _filter;
    std::shared_ptr<UITextField>                     _filterField;

    DelegateHandle _selectionHandle = INVALID_HANDLE;
    DelegateHandle _hierarchyHandle = INVALID_HANDLE;

    void bindLayerDelegates();
    void unbindLayerDelegates();
    void rebuildTree();
    void pullSelectionFromLayer();
    /// `targetId` is the right-clicked row (entity key, `ui:<entry>`,
    /// `ui-root`) or empty for blank space. The menu lists what that target
    /// supports.
    void openContextMenu(const std::string& targetId, const glm::vec2& logicalPoint);
};

} // namespace ya
