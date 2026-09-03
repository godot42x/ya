#include "GameEditor/UI/EditorAutoPropertySection.h"

#include "Core/Reflection/MetadataSupport.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>

namespace ya
{
namespace
{
void applyManipulateSpec(UIDragFloat& drag, const PropertyHandle& binding)
{
    reflection::Meta::ManipulateSpec spec;
    if (!binding.tryGetManipulateSpec(spec)) {
        return;
    }
    drag._min   = spec.min;
    drag._max   = spec.max;
    if (spec.step > 0.0f) {
        drag._speed = spec.step;
    }
}
}

EditorAutoPropertySection::EditorAutoPropertySection(std::string name,
                                                     PropertyGraph graph,
                                                     UndoStack* undo,
                                                     std::string mergeIdentity)
    : UICompoundWidget(std::move(name), "panel")
    , _graph(std::move(graph))
    , _undo(undo)
    , _mergeIdentity(std::move(mergeIdentity))
{
}

std::string EditorAutoPropertySection::mergeKey(const PropertyNode& node, int axis) const
{
    if (axis >= 0) {
        return _mergeIdentity.empty() ? std::format("{}:{}", node.name, axis)
                                      : std::format("{}:{}:{}", _mergeIdentity, node.name, axis);
    }
    return _mergeIdentity.empty() ? node.name : std::format("{}:{}", _mergeIdentity, node.name);
}

void EditorAutoPropertySection::bindDragMerge(UIDragFloat& drag)
{
    if (!_undo) {
        return;
    }
    drag._onDragBegan = [this]() { _undo->beginMerge(); };
    drag._onDragEnded = [this]() { _undo->endMerge(); };
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
                bindDragMerge(*drag);
                drag->_onValueChanged = [this, index = _editors.size(), axis](float value) {
                    PropertyHandle binding = _editors[index].node->binding;
                    auto before = binding.copyVec3();
                    if (before.empty()) return;
                    glm::vec3 patched = before.front();
                    patched[axis] = value;
                    if (!binding.setVec3(patched)) return;
                    if (!_undo) return;
                    auto after = binding.copyVec3();
                    (void)_undo->push({
                        .label    = "Set " + _editors[index].node->displayName,
                        .mergeKey = mergeKey(*_editors[index].node, axis),
                        .undo     = [binding, before]() { binding.restoreVec3(before); },
                        .redo     = [binding, after]() { binding.restoreVec3(after); },
                    });
                };
                slot.vec3.push_back(drag);
                applyManipulateSpec(*drag, node.binding);
                row.child(drag, FBoxSlotArgs{.preferredSize = {72.0f, 22.0f}});
            }
            if (!node.bEditable) {
                for (auto& drag : slot.vec3) drag->setEnabled(false);
            }
        }
        else if (node.valueType == refl::type_index_v<float>) {
            slot.kind = EditorSlot::Kind::Float;
            slot.scalar = std::make_shared<UIDragFloat>(node.name);
            bindDragMerge(*slot.scalar);
            applyManipulateSpec(*slot.scalar, node.binding);
            slot.scalar->_onValueChanged = [this, index = _editors.size()](float value) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyFloat();
                if (before.empty()) return;
                if (!binding.setFloat(value)) return;
                if (!_undo) return;
                auto after = binding.copyFloat();
                (void)_undo->push({
                    .label    = "Set " + _editors[index].node->displayName,
                    .mergeKey = mergeKey(*_editors[index].node),
                    .undo     = [binding, before]() { binding.restoreFloat(before); },
                    .redo     = [binding, after]() { binding.restoreFloat(after); },
                });
            };
            row.child(slot.scalar, FBoxSlotArgs{.preferredSize = {110.0f, 22.0f}});
            if (!node.bEditable) slot.scalar->setEnabled(false);
        }
        else if (node.valueType == refl::type_index_v<bool>) {
            slot.kind = EditorSlot::Kind::Bool;
            slot.boolean = std::make_shared<UICheckBox>(node.name);
            slot.boolean->_onChanged = [this, index = _editors.size()](bool value) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyBool();
                if (before.empty()) return;
                if (!binding.setBool(value)) return;
                if (!_undo) return;
                auto after = binding.copyBool();
                (void)_undo->push({
                    .label = "Set " + _editors[index].node->displayName,
                    .undo  = [binding, before]() { binding.restoreBool(before); },
                    .redo  = [binding, after]() { binding.restoreBool(after); },
                });
            };
            row.child(slot.boolean);
            if (!node.bEditable) slot.boolean->setEnabled(false);
        }
        else if (node.valueType == refl::type_index_v<std::string>) {
            slot.kind = EditorSlot::Kind::String;
            slot.string = std::make_shared<UITextField>(node.name);
            slot.string->_onCommit = [this, index = _editors.size()](const std::string& value) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyString();
                if (before.empty()) return;
                if (!binding.setString(value)) return;
                if (!_undo) return;
                auto after = binding.copyString();
                (void)_undo->push({
                    .label = "Set " + _editors[index].node->displayName,
                    .undo  = [binding, before]() { binding.restoreString(before); },
                    .redo  = [binding, after]() { binding.restoreString(after); },
                });
            };
            row.child(slot.string, FBoxSlotArgs{.preferredSize = {160.0f, 22.0f}});
            if (!node.bEditable) slot.string->setEnabled(false);
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
        const bool hasValidationError = !slot.node->binding.validationError().empty();
        if (slot.kind == EditorSlot::Kind::Vec3) {
            glm::vec3 value{};
            if (!slot.node->binding.tryGetVec3(value)) continue;
            for (int axis = 0; axis < 3; ++axis) {
                if (slot.vec3[axis].get() == focused) continue;
                slot.vec3[axis]->setError(hasValidationError);
                if (slot.node->binding.isMixedVec3Axis(axis)) {
                    slot.vec3[axis]->setMixed(true);
                }
                else {
                    slot.vec3[axis]->setMixed(false);
                    slot.vec3[axis]->setValue(value[axis], false);
                }
            }
        }
        else if (slot.kind == EditorSlot::Kind::Float) {
            float value = 0.0f;
            if (!slot.node->binding.tryGetFloat(value) || slot.scalar.get() == focused) continue;
            slot.scalar->setError(hasValidationError);
            if (slot.node->binding.isMixed()) {
                slot.scalar->setMixed(true);
            }
            else {
                slot.scalar->setMixed(false);
                slot.scalar->setValue(value, false);
            }
        }
        else if (slot.kind == EditorSlot::Kind::Bool) {
            bool value = false;
            if (slot.node->binding.isMixed()) continue;
            if (slot.node->binding.tryGetBool(value) && slot.boolean.get() != focused) slot.boolean->setChecked(value);
        }
        else {
            std::string value;
            if (slot.node->binding.isMixed()) continue;
            if (slot.string.get() != focused) {
                slot.string->setError(hasValidationError);
            }
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
