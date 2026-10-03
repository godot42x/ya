#pragma once

#include "Core/Delegate.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/Inspector/EditorComponentSectionRegistry.h"

#include <memory>
#include <string>
#include <unordered_map>
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
struct UIMenu;
class EditorAutoPropertySection;
struct WidgetTree;
class UndoStack;
struct SelectionModel;

/// Retained Inspector tab. Structure follows Layer selection/hierarchy
/// delegates; property values tick only while attached.
class EditorInspectorTab : public UICompoundWidget
{
  public:
    EditorInspectorTab(EditorLayer& layer, SelectionModel& selection, UndoStack* undo = nullptr);
    ~EditorInspectorTab() override;

    void onAttached() override;
    void onDetached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    SelectionModel* _selection = nullptr;
    UndoStack* _undo = nullptr;
    std::shared_ptr<UITextField> _nameField;
    std::shared_ptr<UIText> _entityText;
    std::shared_ptr<UIText> _emptyText;
    std::shared_ptr<UIContainer> _entityFormHost;
    std::shared_ptr<UIContainer> _widgetEntryHost;
    std::shared_ptr<UIText> _widgetEntryIdText;
    std::shared_ptr<UIText> _widgetEntryTypeText;
    std::shared_ptr<UIButton> _openDesignerButton;
    std::shared_ptr<UIContainer> _instanceHost;
    std::shared_ptr<UIText> _instanceBodyText;
    std::shared_ptr<UIContainer> _projectedHost;
    std::shared_ptr<UIButton> _addComponentButton;
    std::shared_ptr<UIMenu> _addComponentMenu;
    std::vector<std::shared_ptr<UIElement>> _projectedWidgets;
    std::vector<std::shared_ptr<EditorAutoPropertySection>> _projectedSections;
    std::vector<EditorInspectorSectionHost> _customSections;
    std::string _projectedFingerprint;
    DelegateHandle _selectionHandle = INVALID_HANDLE;
    DelegateHandle _hierarchyHandle = INVALID_HANDLE;
    std::unordered_map<std::string, bool> _componentExpanded;

    void bindLayerDelegates();
    void unbindLayerDelegates();
    void refresh();
    void refreshFromTree(WidgetTree& tree);
    void updateInstanceNotice(const std::vector<Entity*>& entities);
    void syncProjectedValues(WidgetTree& tree);
    void rebuildProjected(WidgetTree& tree, const std::vector<Entity*>& entities);
    void openAddComponentMenu();
    void noteSceneMutated();
};

} // namespace ya
