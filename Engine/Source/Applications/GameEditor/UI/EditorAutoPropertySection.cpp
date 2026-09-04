#include "GameEditor/UI/EditorAutoPropertySection.h"

#include "Core/Reflection/MetadataSupport.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"

#include <cmath>
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
                                                     std::string mergeIdentity,
                                                     EditorAssetPickerCallback assetPicker)
    : UICompoundWidget(std::move(name), "panel")
    , _graph(std::move(graph))
    , _undo(undo)
    , _mergeIdentity(std::move(mergeIdentity))
    , _assetPicker(std::move(assetPicker))
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

void EditorAutoPropertySection::commitAssetPath(size_t editorIndex, const std::string& value)
{
    PropertyHandle binding = _editors[editorIndex].node->binding;
    auto before = binding.copyAssetPath();
    if (before.empty()) {
        return;
    }
    if (!binding.setAssetPath(value)) {
        return;
    }
    if (!_undo) {
        return;
    }
    auto after = binding.copyAssetPath();
    (void)_undo->push({
        .label = "Set " + _editors[editorIndex].node->displayName,
        .undo  = [binding, before]() { binding.restoreAssetPath(before); },
        .redo  = [binding, after]() { binding.restoreAssetPath(after); },
    });
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
        if (node.kind == PropertyNode::Kind::Sequence || node.kind == PropertyNode::Kind::Map) {
            slot.kind = EditorSlot::Kind::Container;
            slot.add = ui::button(node.name + "_Add", "+")
                           .setOnClick([this, index = _editors.size()]() {
                               PropertyHandle binding = _editors[index].node->binding;
                               if (!binding.appendEmpty()) {
                                   return;
                               }
                               const size_t added = binding.containerSize() - 1;
                               if (_undo) {
                                   (void)_undo->push({
                                       .label = "Add " + _editors[index].node->displayName,
                                       .undo  = [binding, added]() { binding.removeAtIndex(static_cast<int>(added)); },
                                       .redo  = [binding]() { binding.appendEmpty(); },
                                   });
                               }
                               rebuildRows();
                           })
                           .child(ui::text(node.name + "_AddLabel").setText("+"))
                           .share();
            slot.clear = ui::button(node.name + "_Clear", "Clear")
                             .setOnClick([this, index = _editors.size()]() {
                                 PropertyHandle binding = _editors[index].node->binding;
                                 if (!binding.clearContainer()) {
                                     return;
                                 }
                                 rebuildRows();
                             })
                             .child(ui::text(node.name + "_ClearLabel").setText("Clear"))
                             .share();
            row.child(slot.add, FBoxSlotArgs{.preferredSize = {36.0f, 22.0f}});
            row.child(slot.clear, FBoxSlotArgs{.preferredSize = {56.0f, 22.0f}});
            if (!node.bEditable) {
                slot.add->setEnabled(false);
                slot.clear->setEnabled(false);
            }
        }
        else if (node.bColor && node.binding.isColor() &&
            (node.valueType == refl::type_index_v<glm::vec3> || node.valueType == refl::type_index_v<glm::vec4>)) {
            slot.kind = EditorSlot::Kind::Color;
            slot.color = std::make_shared<UIColorEdit>(node.name);
            slot.color->_onColorChanged = [this, index = _editors.size()](const glm::vec4& value) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyColor();
                if (before.empty()) return;
                if (!binding.setColor(value)) return;
                if (!_undo) return;
                auto after = binding.copyColor();
                (void)_undo->push({
                    .label = "Set " + _editors[index].node->displayName,
                    .undo  = [binding, before]() { binding.restoreColor(before); },
                    .redo  = [binding, after]() { binding.restoreColor(after); },
                });
            };
            row.child(slot.color, FBoxSlotArgs{.preferredSize = {180.0f, 28.0f}});
            if (!node.bEditable) slot.color->setEnabled(false);
        }
        else if (node.valueType == refl::type_index_v<glm::vec2>) {
            slot.kind = EditorSlot::Kind::Vec2;
            for (int axis = 0; axis < 2; ++axis) {
                auto drag = std::make_shared<UIDragFloat>(node.name + std::to_string(axis));
                bindDragMerge(*drag);
                drag->_onValueChanged = [this, index = _editors.size(), axis](float value) {
                    PropertyHandle binding = _editors[index].node->binding;
                    auto before = binding.copyVec2();
                    if (before.empty()) return;
                    glm::vec2 patched = before.front();
                    patched[axis] = value;
                    if (!binding.setVec2(patched)) return;
                    if (!_undo) return;
                    auto after = binding.copyVec2();
                    (void)_undo->push({
                        .label    = "Set " + _editors[index].node->displayName,
                        .mergeKey = mergeKey(*_editors[index].node, axis),
                        .undo     = [binding, before]() { binding.restoreVec2(before); },
                        .redo     = [binding, after]() { binding.restoreVec2(after); },
                    });
                };
                slot.vec2.push_back(drag);
                applyManipulateSpec(*drag, node.binding);
                row.child(drag, FBoxSlotArgs{.preferredSize = {72.0f, 22.0f}});
            }
            if (!node.bEditable) {
                for (auto& drag : slot.vec2) drag->setEnabled(false);
            }
        }
        else if (node.valueType == refl::type_index_v<glm::vec3>) {
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
        else if (node.valueType == refl::type_index_v<glm::vec4>) {
            slot.kind = EditorSlot::Kind::Vec4;
            for (int axis = 0; axis < 4; ++axis) {
                auto drag = std::make_shared<UIDragFloat>(node.name + std::to_string(axis));
                bindDragMerge(*drag);
                drag->_onValueChanged = [this, index = _editors.size(), axis](float value) {
                    PropertyHandle binding = _editors[index].node->binding;
                    auto before = binding.copyVec4();
                    if (before.empty()) return;
                    glm::vec4 patched = before.front();
                    patched[axis] = value;
                    if (!binding.setVec4(patched)) return;
                    if (!_undo) return;
                    auto after = binding.copyVec4();
                    (void)_undo->push({
                        .label    = "Set " + _editors[index].node->displayName,
                        .mergeKey = mergeKey(*_editors[index].node, axis),
                        .undo     = [binding, before]() { binding.restoreVec4(before); },
                        .redo     = [binding, after]() { binding.restoreVec4(after); },
                    });
                };
                slot.vec4.push_back(drag);
                applyManipulateSpec(*drag, node.binding);
                row.child(drag, FBoxSlotArgs{.preferredSize = {54.0f, 22.0f}});
            }
            if (!node.bEditable) {
                for (auto& drag : slot.vec4) drag->setEnabled(false);
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
        else if (node.valueType == refl::type_index_v<int> ||
                 node.valueType == refl::type_index_v<int32_t> ||
                 node.valueType == refl::type_index_v<uint32_t>) {
            slot.kind = EditorSlot::Kind::Integer;
            slot.integer = std::make_shared<UIDragFloat>(node.name);
            bindDragMerge(*slot.integer);
            applyManipulateSpec(*slot.integer, node.binding);
            slot.integer->_speed = slot.integer->_speed > 0.0f ? slot.integer->_speed : 1.0f;
            slot.integer->_decimals = 0;
            slot.integer->_onValueChanged = [this, index = _editors.size()](float value) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyInteger();
                if (before.empty()) return;
                const int64_t rounded = static_cast<int64_t>(std::llround(value));
                if (!binding.setInteger(rounded)) return;
                if (!_undo) return;
                auto after = binding.copyInteger();
                (void)_undo->push({
                    .label    = "Set " + _editors[index].node->displayName,
                    .mergeKey = mergeKey(*_editors[index].node),
                    .undo     = [binding, before]() { binding.restoreInteger(before); },
                    .redo     = [binding, after]() { binding.restoreInteger(after); },
                });
            };
            row.child(slot.integer, FBoxSlotArgs{.preferredSize = {110.0f, 22.0f}});
            if (!node.bEditable) slot.integer->setEnabled(false);
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
        else if (node.binding.isEnum()) {
            slot.kind = EditorSlot::Kind::Enum;
            slot.enumeration = std::make_shared<UIComboBox>(node.name);
            (void)node.binding.enumLabels(slot.enumeration->_items);
            slot.enumeration->_onSelectionChanged = [this, index = _editors.size()](int selected) {
                PropertyHandle binding = _editors[index].node->binding;
                auto before = binding.copyEnum();
                if (before.empty()) return;
                if (!binding.setEnumByIndex(selected)) return;
                if (!_undo) return;
                auto after = binding.copyEnum();
                (void)_undo->push({
                    .label = "Set " + _editors[index].node->displayName,
                    .undo  = [binding, before]() { binding.restoreEnum(before); },
                    .redo  = [binding, after]() { binding.restoreEnum(after); },
                });
            };
            row.child(slot.enumeration, FBoxSlotArgs{.preferredSize = {160.0f, 22.0f}});
            if (!node.bEditable) slot.enumeration->setEnabled(false);
        }
        else if (node.binding.isAssetRef()) {
            slot.kind = EditorSlot::Kind::Asset;
            slot.assetPath = std::make_shared<UITextField>(node.name + "_Path");
            slot.assetPath->_onCommit = [this, index = _editors.size()](const std::string& value) {
                commitAssetPath(index, value);
            };
            slot.browse = ui::button(node.name + "_Browse", "Browse")
                              .setOnClick([this, index = _editors.size()]() {
                                  if (!_assetPicker) {
                                      return;
                                  }
                                  const PropertyHandle binding = _editors[index].node->binding;
                                  const std::optional<EEditorAssetPickerKind> kind = binding.assetRefKind();
                                  if (!kind) {
                                      return;
                                  }
                                  std::string current;
                                  if (!binding.tryGetAssetPath(current)) {
                                      return;
                                  }
                                  _assetPicker(*kind, current, [this, index](std::string path) {
                                      commitAssetPath(index, std::move(path));
                                  });
                              })
                              .child(ui::text(node.name + "_BrowseLabel").setText("Browse"))
                              .share();
            row.child(slot.assetPath, FBoxSlotArgs{.preferredSize = {140.0f, 22.0f}});
            row.child(slot.browse, FBoxSlotArgs{.preferredSize = {56.0f, 22.0f}});
            if (node.binding.assetRefKind() == EEditorAssetPickerKind::Texture) {
                slot.preview = std::make_shared<UIImage>(node.name + "_Preview");
                row.child(slot.preview, FBoxSlotArgs{.preferredSize = {48.0f, 48.0f}});
            }
            if (!node.bEditable) {
                slot.assetPath->setEnabled(false);
                slot.browse->setEnabled(false);
            }
        }
        else {
            continue;
        }
        if (node.kind == PropertyNode::Kind::Value && node.bEditable && node.binding.canMutateContainer() &&
            (node.binding.loc().elementIndex >= 0 || !node.binding.loc().mapKey.empty())) {
            slot.remove = ui::button(node.name + "_Remove", "X")
                              .setOnClick([this, index = _editors.size()]() {
                                  PropertyHandle binding = _editors[index].node->binding;
                                  const int elementIndex = binding.loc().elementIndex;
                                  const std::string mapKey = binding.loc().mapKey;
                                  std::string previousString;
                                  const bool hadString = binding.tryGetString(previousString);
                                  float previousFloat = 0.0f;
                                  const bool hadFloat = binding.tryGetFloat(previousFloat);
                                  const bool removed = !mapKey.empty() ? binding.removeMapKey() : binding.removeAt();
                                  if (!removed) {
                                      return;
                                  }
                                  if (_undo) {
                                      (void)_undo->push({
                                          .label = "Remove " + _editors[index].node->displayName,
                                          .undo  = [binding, mapKey, elementIndex, hadString, previousString, hadFloat, previousFloat]() {
                                              if (!mapKey.empty()) {
                                                  if (!binding.insertMapKey(mapKey)) {
                                                      return;
                                                  }
                                              }
                                              else if (!binding.insertEmptyAt(elementIndex)) {
                                                  return;
                                              }
                                              if (hadString) {
                                                  (void)binding.setString(previousString);
                                              }
                                              else if (hadFloat) {
                                                  (void)binding.setFloat(previousFloat);
                                              }
                                          },
                                          .redo  = [binding, mapKey]() {
                                              if (!mapKey.empty()) {
                                                  (void)binding.removeMapKey();
                                              }
                                              else {
                                                  (void)binding.removeAt();
                                              }
                                          },
                                      });
                                  }
                                  rebuildRows();
                              })
                              .child(ui::text(node.name + "_RemoveLabel").setText("X"))
                              .share();
            row.child(slot.remove, FBoxSlotArgs{.preferredSize = {28.0f, 22.0f}});
        }
        _editors.push_back(std::move(slot));
        rows.child(std::move(row));
    }
    addDetachedChild(rows.release());
}

void EditorAutoPropertySection::rebuildRows()
{
    const type_index_t owner = _graph.getOwnerType();
    std::vector<void*> roots = _graph.getRootInstances();
    _graph = PropertyGraph::project(owner, std::move(roots));
    _editors.clear();
    WidgetTree* tree = getTree();
    if (tree && !getChildren().empty()) {
        tree->detach(*getChildren().front());
    }
    _bConstructed = false;
    prepareForAttach();
}

void EditorAutoPropertySection::sync(WidgetTree& tree)
{
    std::string fingerprint;
    for (const PropertyNode& node : _graph.getNodes()) {
        if (node.kind == PropertyNode::Kind::Sequence || node.kind == PropertyNode::Kind::Map) {
            fingerprint += node.name;
            fingerprint += '=';
            fingerprint += std::to_string(node.binding.containerSize());
            fingerprint += ';';
        }
    }
    if (!_structureFingerprint.empty() && fingerprint != _structureFingerprint) {
        _structureFingerprint = fingerprint;
        rebuildRows();
    }
    else {
        _structureFingerprint = fingerprint;
    }
    UIElement* focused = tree.getFocused();
    for (EditorSlot& slot : _editors) {
        if (!slot.node) continue;
        if (slot.kind == EditorSlot::Kind::Container) {
            continue;
        }
        const bool hasValidationError = !slot.node->binding.validationError().empty();
        if (slot.kind == EditorSlot::Kind::Vec2) {
            glm::vec2 value{};
            if (!slot.node->binding.tryGetVec2(value)) continue;
            for (int axis = 0; axis < 2; ++axis) {
                if (slot.vec2[axis].get() == focused) continue;
                slot.vec2[axis]->setError(hasValidationError);
                if (slot.node->binding.isMixedVecAxis(axis, 2)) {
                    slot.vec2[axis]->setMixed(true);
                }
                else {
                    slot.vec2[axis]->setMixed(false);
                    slot.vec2[axis]->setValue(value[axis], false);
                }
            }
        }
        else if (slot.kind == EditorSlot::Kind::Vec3) {
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
        else if (slot.kind == EditorSlot::Kind::Vec4) {
            glm::vec4 value{};
            if (!slot.node->binding.tryGetVec4(value)) continue;
            for (int axis = 0; axis < 4; ++axis) {
                if (slot.vec4[axis].get() == focused) continue;
                slot.vec4[axis]->setError(hasValidationError);
                if (slot.node->binding.isMixedVecAxis(axis, 4)) {
                    slot.vec4[axis]->setMixed(true);
                }
                else {
                    slot.vec4[axis]->setMixed(false);
                    slot.vec4[axis]->setValue(value[axis], false);
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
        else if (slot.kind == EditorSlot::Kind::Integer) {
            int64_t value = 0;
            if (!slot.node->binding.tryGetInteger(value) || slot.integer.get() == focused) continue;
            slot.integer->setError(hasValidationError);
            if (slot.node->binding.isMixed()) {
                slot.integer->setMixed(true);
            }
            else {
                slot.integer->setMixed(false);
                slot.integer->setValue(static_cast<float>(value), false);
            }
        }
        else if (slot.kind == EditorSlot::Kind::Bool) {
            bool value = false;
            if (slot.node->binding.isMixed()) continue;
            if (slot.node->binding.tryGetBool(value) && slot.boolean.get() != focused) slot.boolean->setChecked(value);
        }
        else if (slot.kind == EditorSlot::Kind::Enum) {
            if (slot.enumeration.get() == focused) continue;
            if (slot.node->binding.isMixed()) {
                slot.enumeration->setMixed(true);
            }
            else {
                int enumIndex = -1;
                slot.enumeration->setMixed(false);
                if (slot.node->binding.tryGetEnumIndex(enumIndex)) {
                    slot.enumeration->setSelectedIndex(enumIndex, false);
                }
            }
        }
        else if (slot.kind == EditorSlot::Kind::Color) {
            if (slot.color.get() == focused) continue;
            if (slot.node->binding.isMixed()) {
                slot.color->setMixed(true);
            }
            else {
                glm::vec4 value{};
                slot.color->setMixed(false);
                if (slot.node->binding.tryGetColor(value)) {
                    slot.color->setColor(value, false);
                }
            }
        }
        else if (slot.kind == EditorSlot::Kind::Asset) {
            if (slot.node->binding.isMixed()) continue;
            if (slot.assetPath.get() != focused) {
                const bool hasError = hasValidationError || slot.node->binding.hasAssetResolveError();
                slot.assetPath->setError(hasError);
            }
            std::string value;
            if (slot.node->binding.tryGetAssetPath(value) && slot.assetPath.get() != focused) {
                slot.assetPath->setText(value);
            }
            if (slot.preview) {
                std::string path;
                if (slot.node->binding.tryGetAssetPath(path) && !slot.node->binding.isMixed()) {
                    slot.preview->_assetPath = path;
                    slot.preview->setResourceMissing(slot.node->binding.hasAssetResolveError());
                }
                else {
                    slot.preview->_assetPath.clear();
                    slot.preview->setResourceMissing(false);
                }
            }
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
    for (const EditorSlot& slot : _editors) {
        if (slot.string.get() == focused || slot.scalar.get() == focused ||
            slot.integer.get() == focused || slot.assetPath.get() == focused) {
            return true;
        }
    }
    return false;
}

} // namespace ya
