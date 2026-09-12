#pragma once

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Controls/TreeView.h"

#include <memory>
#include <string>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;
class UndoStack;

class EditorUIHierarchyTab : public UICompoundWidget
{
  public:
    explicit EditorUIHierarchyTab(EditorLayer& layer);
    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _roots;
    std::shared_ptr<Reactive<std::string>> _selection;
    std::shared_ptr<UITreeView> _treeView;
    std::string _treeFingerprint;
    std::string _selectionFingerprint;

    void refresh();
};

class EditorUIInspectorTab : public UICompoundWidget
{
  public:
    explicit EditorUIInspectorTab(EditorLayer& layer, UndoStack* undo);
    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    UndoStack*   _undo  = nullptr;
    std::shared_ptr<struct UIContainer> _inspectorHost;
    std::shared_ptr<class EditorAutoPropertySection> _inspectorSection;
    std::string _inspectorFingerprint;

    void refresh();
    void rebuildInspector(WidgetTree& tree, UIElement* selected);
};

class EditorUIPaletteTab : public UICompoundWidget
{
  public:
    explicit EditorUIPaletteTab(EditorLayer& layer);

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
};

class EditorUIPreviewTab : public UICompoundWidget
{
  public:
    explicit EditorUIPreviewTab(EditorLayer& layer);
    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIText> _selectionText;

    void refresh();
};

} // namespace ya
