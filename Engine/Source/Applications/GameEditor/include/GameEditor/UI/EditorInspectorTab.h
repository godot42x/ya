#pragma once

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
class EditorAutoPropertySection;
struct WidgetTree;
class UndoStack;

/// Retained Inspector tab. The editor surface hosts this tab but does not own
/// its controls or synchronization state. Component rows come from
/// `PropertyGraph::project`, not handwritten per-type widgets.
class EditorInspectorTab
{
  public:
    explicit EditorInspectorTab(EditorLayer& layer, UndoStack* undo = nullptr)
        : _layer(&layer), _undo(undo)
    {
    }

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync(WidgetTree& tree);
    [[nodiscard]] bool wantsTextInput(WidgetTree& tree) const;
    void reset();

  private:
    EditorLayer* _layer = nullptr;
    UndoStack* _undo = nullptr;
    std::shared_ptr<UITextField> _nameField;
    std::shared_ptr<UIText> _entityText;
    std::shared_ptr<UIText> _emptyText;
    std::shared_ptr<UIContainer> _projectedHost;
    std::vector<std::shared_ptr<UIElement>> _projectedWidgets;
    std::vector<std::shared_ptr<EditorAutoPropertySection>> _projectedSections;
    std::string _projectedFingerprint;

    void rebuildProjected(WidgetTree& tree, const std::vector<Entity*>& entities);
};

} // namespace ya
