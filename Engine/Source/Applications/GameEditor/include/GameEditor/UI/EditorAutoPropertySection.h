#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/EditorAssetPicker.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct UIDragFloat;
struct UICheckBox;
struct UITextField;
struct UIComboBox;
struct UIColorEdit;
struct UIButton;
struct WidgetTree;
class UndoStack;

/// Generic retained editor for the scalar/vector properties in a PropertyGraph.
/// It owns controls, but not selection or component lifetime. Undo closures
/// capture `PropertyHandle` copies; the stack type still has no ECS pointers.
class EditorAutoPropertySection final : public UICompoundWidget
{
  public:
    EditorAutoPropertySection(std::string name,
                              PropertyGraph graph,
                              UndoStack* undo = nullptr,
                              std::string mergeIdentity = {},
                              EditorAssetPickerCallback assetPicker = {});

    void sync(WidgetTree& tree);
    [[nodiscard]] bool wantsTextInput(WidgetTree& tree) const;

  protected:
    void construct() override;

  private:
    struct EditorSlot
    {
        enum class Kind { Vec3, Float, Bool, String, Enum, Color, Asset } kind;
        const PropertyNode* node = nullptr;
        std::vector<std::shared_ptr<UIDragFloat>> vec3;
        std::shared_ptr<UIDragFloat> scalar;
        std::shared_ptr<UICheckBox> boolean;
        std::shared_ptr<UITextField> string;
        std::shared_ptr<UIComboBox> enumeration;
        std::shared_ptr<UIColorEdit> color;
        std::shared_ptr<UITextField> assetPath;
        std::shared_ptr<UIButton> browse;
    };
    PropertyGraph _graph;
    UndoStack* _undo = nullptr;
    std::string _mergeIdentity;
    EditorAssetPickerCallback _assetPicker;
    std::vector<EditorSlot> _editors;

    void bindDragMerge(UIDragFloat& drag);
    void commitAssetPath(size_t editorIndex, const std::string& value);
    [[nodiscard]] std::string mergeKey(const PropertyNode& node, int axis = -1) const;
};

} // namespace ya
