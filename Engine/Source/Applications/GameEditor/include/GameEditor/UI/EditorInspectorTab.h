#pragma once

#include <array>
#include <memory>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct UIText;
struct UITextField;
struct UIDragFloat;
struct WidgetTree;

/// Retained Inspector tab. The editor surface hosts this tab but does not own
/// its controls or synchronization state.
class EditorInspectorTab
{
  public:
    explicit EditorInspectorTab(EditorLayer& layer) : _layer(&layer) {}

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync(WidgetTree& tree);
    [[nodiscard]] bool wantsTextInput(WidgetTree& tree) const;
    void reset();

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<UITextField> _nameField;
    std::shared_ptr<UIText> _entityText;
    std::shared_ptr<UIText> _componentsText;
    std::shared_ptr<UIText> _emptyText;
    std::array<std::shared_ptr<UIDragFloat>, 9> _transformDrags{};
};

} // namespace ya
