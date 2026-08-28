#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/Inspector/PropertyGraph.h"

#include <memory>
#include <vector>

namespace ya
{

struct UIDragFloat;
struct UICheckBox;
struct UITextField;
struct WidgetTree;

/// Generic retained editor for the scalar/vector properties in a PropertyGraph.
/// It owns controls, but not selection or component lifetime.
class EditorAutoPropertySection final : public UICompoundWidget
{
  public:
    EditorAutoPropertySection(std::string name, PropertyGraph graph);

    void sync(WidgetTree& tree);
    [[nodiscard]] bool wantsTextInput(WidgetTree& tree) const;

  protected:
    void construct() override;

  private:
    struct EditorSlot
    {
        enum class Kind { Vec3, Float, Bool, String } kind;
        const PropertyNode* node = nullptr;
        std::vector<std::shared_ptr<UIDragFloat>> vec3;
        std::shared_ptr<UIDragFloat> scalar;
        std::shared_ptr<UICheckBox> boolean;
        std::shared_ptr<UITextField> string;
    };
    PropertyGraph _graph;
    std::vector<EditorSlot> _editors;
};

} // namespace ya
