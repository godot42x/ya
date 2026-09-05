#pragma once

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Controls/TreeView.h"

#include <memory>
#include <string>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;
class UndoStack;

/// Retained UI Designer chrome tab. Document/preview tree stay on
/// UIDesignerPanel; this tab owns palette, tree, and inspector projection.
class EditorUIDesignerTab
{
  public:
    explicit EditorUIDesignerTab(EditorLayer& layer, UndoStack* undo = nullptr)
        : _layer(&layer), _undo(undo)
    {
    }

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync(WidgetTree& tree);

  private:
    EditorLayer* _layer = nullptr;
    UndoStack* _undo = nullptr;

    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIText> _selectionText;
    std::shared_ptr<struct UIButton> _newButton;
    std::shared_ptr<struct UIButton> _saveButton;
    std::shared_ptr<struct UIButton> _closeButton;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _roots;
    std::shared_ptr<Reactive<std::string>> _selection;
    std::shared_ptr<UITreeView> _treeView;
    std::string _treeFingerprint;
    std::shared_ptr<struct UIContainer> _paletteList;
    std::shared_ptr<struct UIContainer> _inspectorHost;
    std::shared_ptr<class EditorAutoPropertySection> _inspectorSection;
    std::string _inspectorFingerprint;
    std::string _selectionFingerprint;

    void rebuildInspector(WidgetTree& tree, UIElement* selected);
};

} // namespace ya
