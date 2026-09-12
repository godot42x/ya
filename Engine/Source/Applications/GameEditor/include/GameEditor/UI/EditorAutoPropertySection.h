#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/EditorAssetPicker.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct UIDragFloat;
struct UICheckBox;
struct UITextField;
struct UIComboBox;
struct UIColorEdit;
struct UIButton;
struct UIImage;
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
                              EditorAssetPickerCallback assetPicker = {},
                              EditorRevealAssetCallback revealAsset = {});

    void sync(WidgetTree& tree);

  protected:
    void construct() override;

  private:
    struct EditorSlot
    {
        enum class Kind { Vec2, Vec3, Vec4, Float, Integer, Bool, String, Enum, Color, Asset, Container } kind;
        const PropertyNode* node = nullptr;
        std::vector<std::shared_ptr<UIDragFloat>> vec2;
        std::vector<std::shared_ptr<UIDragFloat>> vec3;
        std::vector<std::shared_ptr<UIDragFloat>> vec4;
        std::shared_ptr<UIDragFloat> scalar;
        std::shared_ptr<UIDragFloat> integer;
        std::shared_ptr<UICheckBox> boolean;
        std::shared_ptr<UITextField> string;
        std::shared_ptr<UIComboBox> enumeration;
        std::shared_ptr<UIColorEdit> color;
        std::shared_ptr<UITextField> assetPath;
        std::shared_ptr<UIButton> browse;
        std::shared_ptr<UIButton> locate;
        std::shared_ptr<UIImage> preview;
        std::shared_ptr<UIButton> add;
        std::shared_ptr<UIButton> clear;
        std::shared_ptr<UIButton> remove;
    };
    PropertyGraph _graph;
    UndoStack* _undo = nullptr;
    std::string _mergeIdentity;
    EditorAssetPickerCallback _assetPicker;
    EditorRevealAssetCallback _revealAsset;
    std::vector<EditorSlot> _editors;
    std::string _structureFingerprint;
    std::unordered_map<std::string, bool> _groupExpanded;

    void bindDragMerge(UIDragFloat& drag);
    void commitAssetPath(size_t editorIndex, const std::string& value);
    void rebuildRows();
    [[nodiscard]] std::string mergeKey(const PropertyNode& node, int axis = -1) const;
};

} // namespace ya
