#include "GameEditor/UI/EditorAutoPropertySection.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>
#include <variant>

namespace ya
{

EditorAutoPropertySection::EditorAutoPropertySection(std::string name, PropertyGraph graph)
    : UICompoundWidget(std::move(name), "panel"), _graph(std::move(graph))
{
}

void EditorAutoPropertySection::construct()
{
    auto rows = ui::column("AutoPropertyRows").setSpacing(4.0f);
    for (const PropertyNode& node : _graph.getNodes()) {
        if (!node.bVisible) continue;
        auto row = ui::row("PropertyRow_" + node.name).setSpacing(6.0f);
        row.child(ui::text("PropertyLabel_" + node.name).setText(node.displayName),
                  FBoxSlotArgs{.preferredSize = {100.0f, 22.0f}});

        EditorSlot slot;
        slot.node = &node;
        if (node.valueType == refl::type_index_v<glm::vec3>) {
            slot.kind = EditorSlot::Kind::Vec3;
            for (int axis = 0; axis < 3; ++axis) {
                auto drag = std::make_shared<UIDragFloat>(node.name + std::to_string(axis));
                drag->_onValueChanged = [this, index = _editors.size(), axis](float value) {
                    glm::vec3 vector{};
                    if (!_editors[index].node->binding.tryGetVec3(vector)) return;
                    vector[axis] = value;
                    _editors[index].node->binding.setVec3(vector);
                };
                slot.vec3.push_back(drag);
                row.child(drag, FBoxSlotArgs{.preferredSize = {72.0f, 22.0f}});
            }
        }
        else if (node.valueType == refl::type_index_v<float>) {
            slot.kind = EditorSlot::Kind::Float;
            slot.scalar = std::make_shared<UIDragFloat>(node.name);
            slot.scalar->_onValueChanged = [this, index = _editors.size()](float value) { _editors[index].node->binding.setFloat(value); };
            row.child(slot.scalar, FBoxSlotArgs{.preferredSize = {110.0f, 22.0f}});
        }
        else if (node.valueType == refl::type_index_v<bool>) {
            slot.kind = EditorSlot::Kind::Bool;
            slot.boolean = std::make_shared<UICheckBox>(node.name);
            slot.boolean->_onChanged = [this, index = _editors.size()](bool value) { _editors[index].node->binding.setBool(value); };
            row.child(slot.boolean);
        }
        else if (node.valueType == refl::type_index_v<std::string>) {
            slot.kind = EditorSlot::Kind::String;
            slot.string = std::make_shared<UITextField>(node.name);
            slot.string->_onCommit = [this, index = _editors.size()](const std::string& value) { _editors[index].node->binding.setString(value); };
            row.child(slot.string, FBoxSlotArgs{.preferredSize = {160.0f, 22.0f}});
        }
        else {
            continue;
        }
        _editors.push_back(std::move(slot));
        rows.child(std::move(row));
    }
    addDetachedChild(rows.release());
}

void EditorAutoPropertySection::sync(WidgetTree& tree)
{
    UIElement* focused = tree.getFocused();
    for (EditorSlot& slot : _editors) {
        if (!slot.node) continue;
        if (slot.kind == EditorSlot::Kind::Vec3) {
            glm::vec3 value{};
            if (!slot.node->binding.tryGetVec3(value)) continue;
            for (int axis = 0; axis < 3; ++axis) if (slot.vec3[axis].get() != focused) slot.vec3[axis]->setValue(value[axis]);
        }
        else if (slot.kind == EditorSlot::Kind::Float) {
            float value = 0.0f;
            if (slot.node->binding.tryGetFloat(value) && slot.scalar.get() != focused) slot.scalar->setValue(value);
        }
        else if (slot.kind == EditorSlot::Kind::Bool) {
            bool value = false;
            if (slot.node->binding.tryGetBool(value) && slot.boolean.get() != focused) slot.boolean->setChecked(value);
        }
        else {
            std::string value;
            if (slot.node->binding.tryGetString(value) && slot.string.get() != focused) slot.string->setText(value);
        }
    }
}

bool EditorAutoPropertySection::wantsTextInput(WidgetTree& tree) const
{
    UIElement* focused = tree.getFocused();
    for (const EditorSlot& slot : _editors) if (slot.string.get() == focused || slot.scalar.get() == focused) return true;
    return false;
}

} // namespace ya
