#pragma once

#include "Core/Delegate.h"
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

/// Retained Inspector tab. Structure follows Layer selection/hierarchy
/// delegates; property values tick only while attached.
class EditorInspectorTab : public UICompoundWidget
{
  public:
    explicit EditorInspectorTab(EditorLayer& layer, UndoStack* undo = nullptr);
    ~EditorInspectorTab() override;

    void onAttached() override;
    void onDetached() override;
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
    DelegateHandle _selectionHandle = INVALID_HANDLE;
    DelegateHandle _hierarchyHandle = INVALID_HANDLE;

    void bindLayerDelegates();
    void unbindLayerDelegates();
    void refresh();
    void refreshFromTree(WidgetTree& tree);
    void syncProjectedValues(WidgetTree& tree);
    void rebuildProjected(WidgetTree& tree, const std::vector<Entity*>& entities);
};

} // namespace ya
