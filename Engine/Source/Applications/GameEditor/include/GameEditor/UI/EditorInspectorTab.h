#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct Entity;
struct UIElement;
struct UIText;
struct UITextField;
struct UIContainer;
struct UIButton;
class EditorAutoPropertySection;
struct WidgetTree;
class UndoStack;

/// Retained Inspector tab. Component rows come from `PropertyGraph::project`.
class EditorInspectorTab : public UICompoundWidget
{
  public:
    explicit EditorInspectorTab(EditorLayer& layer, UndoStack* undo = nullptr);

    void onAttached() override;
    void tick(float deltaSeconds) override;
    [[nodiscard]] bool wantsTextInput() const;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    UndoStack* _undo = nullptr;
    std::shared_ptr<UITextField> _nameField;
    std::shared_ptr<UIText> _entityText;
    std::shared_ptr<UIText> _emptyText;
    std::shared_ptr<UIContainer> _entityFormHost;
    std::shared_ptr<UIContainer> _widgetEntryHost;
    std::shared_ptr<UIText> _widgetEntryIdText;
    std::shared_ptr<UIText> _widgetEntryTypeText;
    std::shared_ptr<UIButton> _openDesignerButton;
    std::shared_ptr<UIContainer> _projectedHost;
    std::vector<std::shared_ptr<UIElement>> _projectedWidgets;
    std::vector<std::shared_ptr<EditorAutoPropertySection>> _projectedSections;
    std::string _projectedFingerprint;

    void refresh();
    void refreshFromTree(WidgetTree& tree);
    void rebuildProjected(WidgetTree& tree, const std::vector<Entity*>& entities);
};

} // namespace ya
