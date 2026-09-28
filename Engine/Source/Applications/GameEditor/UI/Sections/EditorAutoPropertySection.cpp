#include "GameEditor/UI/Sections/EditorAutoPropertySection.h"

#include "Core/Reflection/MetadataSupport.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Declarative/LayoutBuilders.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/ColorEdit.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/Shell/EditorTheme.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

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

void applyEditorField(UIElement& widget, std::string_view familyKey)
{
    widget.setStyleKey(editorStyle(familyKey));
}

[[nodiscard]] FBoxSlotArgs labelColumnSlot()
{
    return {.preferredSize = {editor_density::kLabelColumn, editor_density::kRowHeight}};
}

[[nodiscard]] FBoxSlotArgs labelTopSlot()
{
    return {
        .crossAlignment = EUIBoxSlotCrossAlignment::Start,
        .preferredSize  = {editor_density::kLabelColumn, editor_density::kRowHeight},
    };
}

[[nodiscard]] FBoxSlotArgs fillPathSlot()
{
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {160.0f, editor_density::kRowHeight},
    };
}

[[nodiscard]] FBoxSlotArgs fillColorSlot()
{
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {0.0f, editor_density::kColorRowHeight},
    };
}

[[nodiscard]] FBoxSlotArgs fillControlSlot()
{
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {0.0f, editor_density::kRowHeight},
    };
}

[[nodiscard]] FBoxSlotArgs fillAssetSlot(bool bTexture)
{
    float height = editor_density::kRowHeight * 2.0f + editor_density::kControlSpacing;
    if (bTexture) {
        height += editor_density::kControlSpacing + editor_density::kAssetThumbSize;
    }
    return {
        .sizeRule      = EUIBoxSlotSizeRule::Fill,
        .preferredSize = {160.0f, height},
    };
}

[[nodiscard]] FBoxSlotArgs fixedControlSlot(float width)
{
    return {.preferredSize = {width, editor_density::kRowHeight}};
}

[[nodiscard]] std::vector<std::string> splitPropertyGroupPath(const std::string& group)
{
    std::vector<std::string> parts;
    if (group.empty()) {
        return parts;
    }
    constexpr std::string_view kSep = " / ";
    size_t start = 0;
    while (start <= group.size()) {
        const size_t found = group.find(kSep, start);
        if (found == std::string::npos) {
            parts.push_back(group.substr(start));
            break;
        }
        parts.push_back(group.substr(start, found - start));
        start = found + kSep.size();
    }
    return parts;
}

[[nodiscard]] std::string joinPropertyGroupKey(const std::vector<std::string>& parts, size_t count)
{
    std::string key;
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            key += " / ";
        }
        key += parts[i];
    }
    return key;
}

/// Editor value codecs: the one place that knows which PropertyHandle API
/// snapshots, writes and restores each value kind. The generic codec covers
/// plain reflected values; bool / integer / enum / color / asset have their
/// own snapshot calls.
template <typename V>
struct FValueCodec
{
    static std::vector<V> copy(const PropertyHandle& binding)
    {
        return binding.template copy<V>();
    }
    static bool write(const PropertyHandle& binding, const V& value)
    {
        return binding.set(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<V>& values)
    {
        return binding.restore(values);
    }
};

// vector<bool> snapshots are banned on PropertyHandle; copyBool stores uint8.
template <>
struct FValueCodec<bool>
{
    static std::vector<uint8_t> copy(const PropertyHandle& binding)
    {
        return binding.copyBool();
    }
    static bool write(const PropertyHandle& binding, bool value)
    {
        return binding.set(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<uint8_t>& values)
    {
        return binding.restoreBool(values);
    }
};

template <>
struct FValueCodec<int64_t>
{
    static std::vector<int64_t> copy(const PropertyHandle& binding)
    {
        return binding.copyInteger();
    }
    static bool write(const PropertyHandle& binding, int64_t value)
    {
        return binding.setInteger(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<int64_t>& values)
    {
        return binding.restoreInteger(values);
    }
};

struct FEnumTag {};
template <>
struct FValueCodec<FEnumTag>
{
    static std::vector<int64_t> copy(const PropertyHandle& binding)
    {
        return binding.copyEnum();
    }
    static bool write(const PropertyHandle& binding, int value)
    {
        return binding.setEnumByIndex(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<int64_t>& values)
    {
        return binding.restoreEnum(values);
    }
};

struct FColorTag {};
template <>
struct FValueCodec<FColorTag>
{
    static std::vector<glm::vec4> copy(const PropertyHandle& binding)
    {
        return binding.copyColor();
    }
    static bool write(const PropertyHandle& binding, const glm::vec4& value)
    {
        return binding.setColor(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<glm::vec4>& values)
    {
        return binding.restoreColor(values);
    }
};

struct FAssetTag {};
template <>
struct FValueCodec<FAssetTag>
{
    static std::vector<std::string> copy(const PropertyHandle& binding)
    {
        return binding.copyAssetPath();
    }
    static bool write(const PropertyHandle& binding, const std::string& value)
    {
        return binding.setAssetPath(value);
    }
    static bool restore(const PropertyHandle& binding, const std::vector<std::string>& values)
    {
        return binding.restoreAssetPath(values);
    }
};

template <int N> struct FVecType;
template <> struct FVecType<2> { using type = glm::vec2; };
template <> struct FVecType<3> { using type = glm::vec3; };
template <> struct FVecType<4> { using type = glm::vec4; };

/// Group expander stack: creates / aligns nested expanders for a "A / B"
/// group path and attaches rows under the innermost one. Lives only for the
/// duration of construct(); the expanders outlive it and capture the section
/// -owned expanded map directly.
class FGroupNester
{
  public:
    FGroupNester(ui::UIContainerWidgetBuilder& rows, std::unordered_map<std::string, bool>& expanded)
        : _rows(rows)
        , _expanded(expanded)
    {
    }

    void open(const std::string& group)
    {
        if (group == _current) {
            return;
        }
        _current = group;
        const std::vector<std::string> parts = splitPropertyGroupPath(group);
        size_t common = 0;
        while (common < _segments.size() && common < parts.size() &&
               _segments[common] == parts[common]) {
            ++common;
        }
        while (_segments.size() > common) {
            _segments.pop_back();
            _stack.pop_back();
        }
        for (size_t i = common; i < parts.size(); ++i) {
            const std::string key = joinPropertyGroupKey(parts, i + 1);
            bool expanded = true;
            if (const auto it = _expanded.find(key); it != _expanded.end()) {
                expanded = it->second;
            }
            else {
                _expanded.emplace(key, true);
            }
            auto expander = ui::treeNode("PropertyGroup_" + key)
                                .setTitle(parts[i])
                                .setExpanded(expanded)
                                .setSpacing(editor_density::kRowSpacing)
                                .share();
            expander->_onExpandedChanged = [&map = _expanded, key](bool value) {
                map[key] = value;
            };
            if (_stack.empty()) {
                _rows.child(expander);
            }
            else {
                _stack.back()->addDetachedChild(expander);
            }
            _stack.push_back(std::move(expander));
            _segments.push_back(parts[i]);
        }
    }

    void attach(ui::UIContainerWidgetBuilder row)
    {
        if (_stack.empty()) {
            _rows.child(std::move(row));
        }
        else {
            _stack.back()->addDetachedChild(std::move(row).release());
        }
    }

  private:
    ui::UIContainerWidgetBuilder&          _rows;
    std::unordered_map<std::string, bool>& _expanded;
    std::vector<std::shared_ptr<UIExpander>> _stack;
    std::vector<std::string>                 _segments;
    std::string                              _current;
};

} // namespace

EditorAutoPropertySection::EditorAutoPropertySection(std::string name,
                                                     PropertyGraph graph,
                                                     UndoStack* undo,
                                                     std::string mergeIdentity,
                                                     EditorAssetPickerCallback assetPicker,
                                                     EditorRevealAssetCallback revealAsset)
    : UICompoundWidget(std::move(name))
    , _graph(std::move(graph))
    , _undo(undo)
    , _mergeIdentity(std::move(mergeIdentity))
    , _assetPicker(std::move(assetPicker))
    , _revealAsset(std::move(revealAsset))
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
    if (_commitSink.commit) {
        drag._onDragBegan = [this]() {
            if (_commitSink.beginGesture) {
                _commitSink.beginGesture();
            }
        };
        drag._onDragEnded = [this]() {
            if (_commitSink.endGesture) {
                _commitSink.endGesture();
            }
        };
        return;
    }
    if (!_undo) {
        return;
    }
    drag._onDragBegan = [this]() { _undo->beginMerge(); };
    drag._onDragEnded = [this]() { _undo->endMerge(); };
}

void EditorAutoPropertySection::pushUndo(FUndoCommand command)
{
    if (_commitSink.commit) {
        _commitSink.commit(command.label, command.mergeKey);
    }
    else if (_undo) {
        (void)_undo->push(std::move(command));
    }
    if (_onMutated) {
        _onMutated();
    }
}

template <typename Codec, typename V, typename S>
void EditorAutoPropertySection::pushValueUndo(size_t index, const V& value, std::string mergeKey, std::vector<S> before)
{
    const PropertyNode* node = _editors[index].node;
    PropertyHandle binding = node->binding;
    if (!Codec::write(binding, value)) {
        return;
    }
    auto after = Codec::copy(binding);
    pushUndo({
        .label    = "Set " + node->displayName,
        .mergeKey = std::move(mergeKey),
        .undo     = [binding, before]() { Codec::restore(binding, before); },
        .redo     = [binding, after]() { Codec::restore(binding, after); },
    });
}

template <typename Codec, typename V>
void EditorAutoPropertySection::commitWithCodec(size_t index, const V& value, std::string mergeKey)
{
    PropertyHandle binding = _editors[index].node->binding;
    auto before = Codec::copy(binding);
    if (before.empty()) {
        return;
    }
    pushValueUndo<Codec>(index, value, std::move(mergeKey), std::move(before));
}

template <typename V>
void EditorAutoPropertySection::commitValue(size_t index, const V& value, std::string mergeKey)
{
    commitWithCodec<FValueCodec<V>>(index, value, std::move(mergeKey));
}

template <typename V>
void EditorAutoPropertySection::commitKnownBefore(size_t index, const V& value, std::string mergeKey, std::vector<V> before)
{
    pushValueUndo<FValueCodec<V>>(index, value, std::move(mergeKey), std::move(before));
}

void EditorAutoPropertySection::commitColor(size_t index, const glm::vec4& value)
{
    commitWithCodec<FValueCodec<FColorTag>>(index, value, {});
}

void EditorAutoPropertySection::commitEnumIndex(size_t index, int selectedIndex)
{
    commitWithCodec<FValueCodec<FEnumTag>>(index, selectedIndex, {});
}

void EditorAutoPropertySection::commitAssetPath(size_t editorIndex, const std::string& value)
{
    commitWithCodec<FValueCodec<FAssetTag>>(editorIndex, value, {});
}

std::string EditorAutoPropertySection::structureFingerprint() const
{
    std::string fingerprint;
    for (const PropertyNode& node : _graph.getNodes()) {
        if (node.kind != PropertyNode::Kind::Sequence && node.kind != PropertyNode::Kind::Map) {
            continue;
        }
        fingerprint += node.name;
        fingerprint += '=';
        fingerprint += std::to_string(node.binding.containerSize());
        fingerprint += ';';
    }
    return fingerprint;
}

void EditorAutoPropertySection::buildContainerEditor(const PropertyNode& node,
                                                     ui::UIContainerWidgetBuilder& row,
                                                     size_t index)
{
    auto add = ui::button(node.name + "_Add", "+")
                   .setOnClick([this, index]() {
                       PropertyHandle binding = _editors[index].node->binding;
                       if (!binding.appendEmpty()) {
                           return;
                       }
                       const size_t added = binding.containerSize() - 1;
                       pushUndo({
                           .label = "Add " + _editors[index].node->displayName,
                           .undo  = [binding, added]() { binding.removeAtIndex(static_cast<int>(added)); },
                           .redo  = [binding]() { binding.appendEmpty(); },
                       });
                       rebuildRows();
                   })
                   .child(ui::text(node.name + "_AddLabel").setText("+"))
                   .share();
    auto clear = ui::button(node.name + "_Clear", "Clear")
                     .setOnClick([this, index]() {
                         PropertyHandle binding = _editors[index].node->binding;
                         if (!binding.clearContainer()) {
                             return;
                         }
                         rebuildRows();
                     })
                     .child(ui::text(node.name + "_ClearLabel").setText("Clear"))
                     .share();
    row.child(add, fixedControlSlot(36.0f));
    row.child(clear, fixedControlSlot(56.0f));
    if (!node.bEditable) {
        add->setEnabled(false);
        clear->setEnabled(false);
    }
}

void EditorAutoPropertySection::buildColorEditor(const PropertyNode& node,
                                                 ui::UIContainerWidgetBuilder& row,
                                                 EditorSlot& slot,
                                                 size_t index)
{
    auto color = std::make_shared<UIColorEdit>(node.name);
    applyEditorField(*color, StyleKey::ColorEdit);
    color->setChannelCount(node.valueType == refl::type_index_v<glm::vec3> ? 3 : 4);
    color->_onColorChanged = [this, index](const glm::vec4& value) {
        commitColor(index, value);
    };
    row.child(color, fillColorSlot());
    if (!node.bEditable) {
        color->setEnabled(false);
    }
    slot.pull = [node = &node, color](UIElement* focused) {
        if (color.get() == focused) {
            return;
        }
        if (node->binding.isMixed()) {
            color->setMixed(true);
            return;
        }
        color->setMixed(false);
        glm::vec4 value{};
        if (node->binding.tryGetColor(value)) {
            color->setColor(value, false);
        }
    };
}

template <int N>
void EditorAutoPropertySection::buildVecEditor(const PropertyNode& node,
                                               ui::UIContainerWidgetBuilder& row,
                                               EditorSlot& slot,
                                               size_t index)
{
    using VecT = typename FVecType<N>::type;
    std::vector<std::shared_ptr<UIDragFloat>> drags;
    drags.reserve(N);
    for (int axis = 0; axis < N; ++axis) {
        auto drag = std::make_shared<UIDragFloat>(node.name + std::to_string(axis));
        applyEditorField(*drag, StyleKey::DragFloat);
        bindDragMerge(*drag);
        applyManipulateSpec(*drag, node.binding);
        drag->_onValueChanged = [this, index, axis](float value) {
            const PropertyNode* dragged = _editors[index].node;
            PropertyHandle binding = dragged->binding;
            auto before = binding.copy<VecT>();
            if (before.empty()) {
                return;
            }
            VecT patched = before.front();
            patched[axis] = value;
            commitKnownBefore(index, patched, mergeKey(*dragged, axis), std::move(before));
        };
        row.child(drag, fillControlSlot());
        drags.push_back(std::move(drag));
    }
    if (!node.bEditable) {
        for (auto& drag : drags) {
            drag->setEnabled(false);
        }
    }
    slot.pull = [node = &node, drags = std::move(drags)](UIElement* focused) {
        const bool hasValidationError = !node->binding.validationError().empty();
        VecT value{};
        if (!node->binding.tryGet(value)) {
            return;
        }
        for (int axis = 0; axis < N; ++axis) {
            if (drags[axis].get() == focused) {
                continue;
            }
            drags[axis]->setError(hasValidationError);
            if (node->binding.isMixedVecAxis(axis, N)) {
                drags[axis]->setMixed(true);
            }
            else {
                drags[axis]->setMixed(false);
                drags[axis]->setValue(value[axis], false);
            }
        }
    };
}

void EditorAutoPropertySection::buildScalarEditor(const PropertyNode& node,
                                                  ui::UIContainerWidgetBuilder& row,
                                                  EditorSlot& slot,
                                                  size_t index)
{
    auto drag = std::make_shared<UIDragFloat>(node.name);
    applyEditorField(*drag, StyleKey::DragFloat);
    bindDragMerge(*drag);
    applyManipulateSpec(*drag, node.binding);
    drag->_onValueChanged = [this, index](float value) {
        commitValue(index, value, mergeKey(*_editors[index].node));
    };
    row.child(drag, fillControlSlot());
    if (!node.bEditable) {
        drag->setEnabled(false);
    }
    slot.pull = [node = &node, drag](UIElement* focused) {
        const bool hasValidationError = !node->binding.validationError().empty();
        float value = 0.0f;
        if (!node->binding.tryGet(value) || drag.get() == focused) {
            return;
        }
        drag->setError(hasValidationError);
        if (node->binding.isMixed()) {
            drag->setMixed(true);
        }
        else {
            drag->setMixed(false);
            drag->setValue(value, false);
        }
    };
}

void EditorAutoPropertySection::buildIntegerEditor(const PropertyNode& node,
                                                   ui::UIContainerWidgetBuilder& row,
                                                   EditorSlot& slot,
                                                   size_t index)
{
    auto drag = std::make_shared<UIDragFloat>(node.name);
    applyEditorField(*drag, StyleKey::DragFloat);
    bindDragMerge(*drag);
    applyManipulateSpec(*drag, node.binding);
    drag->_speed   = drag->_speed > 0.0f ? drag->_speed : 1.0f;
    drag->_decimals = 0;
    drag->_onValueChanged = [this, index](float value) {
        commitValue(index, static_cast<int64_t>(std::llround(value)), mergeKey(*_editors[index].node));
    };
    row.child(drag, fillControlSlot());
    if (!node.bEditable) {
        drag->setEnabled(false);
    }
    slot.pull = [node = &node, drag](UIElement* focused) {
        const bool hasValidationError = !node->binding.validationError().empty();
        int64_t value = 0;
        if (!node->binding.tryGetInteger(value) || drag.get() == focused) {
            return;
        }
        drag->setError(hasValidationError);
        if (node->binding.isMixed()) {
            drag->setMixed(true);
        }
        else {
            drag->setMixed(false);
            drag->setValue(static_cast<float>(value), false);
        }
    };
}

void EditorAutoPropertySection::buildBoolEditor(const PropertyNode& node,
                                                ui::UIContainerWidgetBuilder& row,
                                                EditorSlot& slot,
                                                size_t index)
{
    auto boolean = std::make_shared<UICheckBox>(node.name);
    boolean->_onChanged = [this, index](bool value) {
        commitValue(index, value);
    };
    row.child(boolean);
    if (!node.bEditable) {
        boolean->setEnabled(false);
    }
    slot.pull = [node = &node, boolean](UIElement* focused) {
        if (node->binding.isMixed()) {
            return;
        }
        bool value = false;
        if (node->binding.tryGet(value) && boolean.get() != focused) {
            boolean->setChecked(value);
        }
    };
}

void EditorAutoPropertySection::buildStringEditor(const PropertyNode& node,
                                                  ui::UIContainerWidgetBuilder& row,
                                                  EditorSlot& slot,
                                                  size_t index)
{
    auto string = std::make_shared<UITextField>(node.name);
    applyEditorField(*string, StyleKey::TextField);
    string->_onCommit = [this, index](const std::string& value) {
        commitValue(index, value);
    };
    row.child(string, fillControlSlot());
    if (!node.bEditable) {
        string->setEnabled(false);
    }
    slot.pull = [node = &node, string](UIElement* focused) {
        if (node->binding.isMixed()) {
            return;
        }
        if (string.get() != focused) {
            string->setError(!node->binding.validationError().empty());
        }
        std::string value;
        if (node->binding.tryGet(value) && string.get() != focused) {
            string->setText(value);
        }
    };
}

void EditorAutoPropertySection::buildEnumEditor(const PropertyNode& node,
                                                ui::UIContainerWidgetBuilder& row,
                                                EditorSlot& slot,
                                                size_t index)
{
    auto enumeration = std::make_shared<UIComboBox>(node.name);
    applyEditorField(*enumeration, StyleKey::ComboBox);
    (void)node.binding.enumLabels(enumeration->_items);
    enumeration->_onSelectionChanged = [this, index](int selected) {
        commitEnumIndex(index, selected);
    };
    row.child(enumeration, fillControlSlot());
    if (!node.bEditable) {
        enumeration->setEnabled(false);
    }
    slot.pull = [node = &node, enumeration](UIElement* focused) {
        if (enumeration.get() == focused) {
            return;
        }
        if (node->binding.isMixed()) {
            enumeration->setMixed(true);
            return;
        }
        enumeration->setMixed(false);
        int enumIndex = -1;
        if (node->binding.tryGetEnumIndex(enumIndex)) {
            enumeration->setSelectedIndex(enumIndex, false);
        }
    };
}

void EditorAutoPropertySection::buildAssetEditor(const PropertyNode& node,
                                                 ui::UIContainerWidgetBuilder& row,
                                                 EditorSlot& slot,
                                                 size_t index)
{
    auto assetPath = std::make_shared<UITextField>(node.name + "_Path");
    applyEditorField(*assetPath, StyleKey::TextField);
    assetPath->_onCommit = [this, index](const std::string& value) {
        commitAssetPath(index, value);
    };
    auto browse = ui::button(node.name + "_Browse", "Browse")
                      .setContentPadding({6.0f, 2.0f})
                      .setOnClick([this, index]() {
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
    auto locate = ui::button(node.name + "_Locate", "Show")
                      .setContentPadding({6.0f, 2.0f})
                      .setOnClick([this, index]() {
                          if (!_revealAsset) {
                              return;
                          }
                          std::string current;
                          if (!_editors[index].node->binding.tryGetAssetPath(current) || current.empty()) {
                              return;
                          }
                          _revealAsset(std::move(current));
                      })
                      .child(ui::text(node.name + "_LocateLabel").setText("Show"))
                      .share();
    auto pathRow = ui::row(node.name + "_PathRow").setSpacing(editor_density::kControlSpacing);
    pathRow.child(assetPath, fillPathSlot());
    auto buttonRow = ui::row(node.name + "_AssetButtons").setSpacing(editor_density::kControlSpacing);
    buttonRow.child(browse, fixedControlSlot(editor_density::kBrowseButtonWidth));
    buttonRow.child(locate, fixedControlSlot(editor_density::kLocateButtonWidth));
    auto assetCol = ui::column(node.name + "_AssetCol").setSpacing(editor_density::kControlSpacing);
    assetCol.child(std::move(pathRow),
                   FBoxSlotArgs{.preferredSize = {0.0f, editor_density::kRowHeight}});
    assetCol.child(std::move(buttonRow),
                   FBoxSlotArgs{.preferredSize = {0.0f, editor_density::kRowHeight}});
    std::shared_ptr<UIImage> preview;
    if (node.binding.assetRefKind() == EEditorAssetPickerKind::Texture) {
        preview = std::make_shared<UIImage>(node.name + "_Preview");
        preview->setScaleMode(EImageScaleMode::Contain);
        assetCol.child(preview,
                       FBoxSlotArgs{
                           .crossAlignment = EUIBoxSlotCrossAlignment::Start,
                           .preferredSize = {editor_density::kAssetThumbSize,
                                             editor_density::kAssetThumbSize},
                       });
    }
    row.child(std::move(assetCol),
              fillAssetSlot(node.binding.assetRefKind() == EEditorAssetPickerKind::Texture));
    if (!node.bEditable) {
        assetPath->setEnabled(false);
        browse->setEnabled(false);
    }
    slot.pull = [node = &node, assetPath, locate, preview = std::move(preview)](UIElement* focused) {
        if (node->binding.isMixed()) {
            return;
        }
        if (assetPath.get() != focused) {
            assetPath->setError(!node->binding.validationError().empty() || node->binding.hasAssetResolveError());
        }
        std::string value;
        if (node->binding.tryGetAssetPath(value) && assetPath.get() != focused) {
            assetPath->setText(value);
        }
        if (locate) {
            std::string locatePath = value;
            if (locatePath.empty()) {
                (void)node->binding.tryGetAssetPath(locatePath);
            }
            locate->setEnabled(!locatePath.empty());
        }
        if (preview) {
            std::string path;
            if (node->binding.tryGetAssetPath(path) && !node->binding.isMixed()) {
                preview->setAssetPath(std::move(path));
                preview->setScaleMode(EImageScaleMode::Contain);
                preview->setResourceMissing(node->binding.hasAssetResolveError());
            }
            else {
                preview->setAssetPath({});
                preview->setResourceMissing(false);
            }
        }
    };
}

void EditorAutoPropertySection::appendRemoveButton(const PropertyNode& node,
                                                   ui::UIContainerWidgetBuilder& row,
                                                   size_t index)
{
    auto remove = ui::button(node.name + "_Remove", "X")
                      .setOnClick([this, index]() {
                          PropertyHandle binding = _editors[index].node->binding;
                          const int elementIndex = binding.slot().elementIndex;
                          const std::optional<std::string> mapKey = binding.slot().mapKey;
                          std::string previousString;
                          const bool hadString = binding.tryGet(previousString);
                          float previousFloat = 0.0f;
                          const bool hadFloat = binding.tryGet(previousFloat);
                          const bool removed = mapKey.has_value() ? binding.removeMapKey() : binding.removeAt();
                          if (!removed) {
                              return;
                          }
                          pushUndo({
                              .label = "Remove " + _editors[index].node->displayName,
                              .undo  = [binding, mapKey, elementIndex, hadString, previousString, hadFloat, previousFloat]() {
                                  if (mapKey.has_value()) {
                                      if (!binding.insertMapKey(*mapKey)) {
                                          return;
                                      }
                                  }
                                  else if (!binding.insertEmptyAt(elementIndex)) {
                                      return;
                                  }
                                  if (hadString) {
                                      (void)binding.set(previousString);
                                  }
                                  else if (hadFloat) {
                                      (void)binding.set(previousFloat);
                                  }
                              },
                              .redo  = [binding, mapKey]() {
                                  if (mapKey.has_value()) {
                                      (void)binding.removeMapKey();
                                  }
                                  else {
                                      (void)binding.removeAt();
                                  }
                              },
                          });
                          rebuildRows();
                      })
                      .child(ui::text(node.name + "_RemoveLabel").setText("X"))
                      .share();
    row.child(remove, fixedControlSlot(28.0f));
}

bool EditorAutoPropertySection::buildEditor(const PropertyNode& node,
                                            ui::UIContainerWidgetBuilder& row,
                                            EditorSlot& slot)
{
    const size_t index = _editors.size();
    if (node.kind == PropertyNode::Kind::Sequence || node.kind == PropertyNode::Kind::Map) {
        buildContainerEditor(node, row, index);
    }
    else if (node.bColor && node.binding.isColor() &&
             (node.valueType == refl::type_index_v<glm::vec3> || node.valueType == refl::type_index_v<glm::vec4>)) {
        buildColorEditor(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<glm::vec2>) {
        buildVecEditor<2>(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<glm::vec3>) {
        buildVecEditor<3>(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<glm::vec4>) {
        buildVecEditor<4>(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<float>) {
        buildScalarEditor(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<int> || node.valueType == refl::type_index_v<int32_t> ||
             node.valueType == refl::type_index_v<uint32_t>) {
        buildIntegerEditor(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<bool>) {
        buildBoolEditor(node, row, slot, index);
    }
    else if (node.valueType == refl::type_index_v<std::string>) {
        buildStringEditor(node, row, slot, index);
    }
    else if (node.binding.isEnum()) {
        buildEnumEditor(node, row, slot, index);
    }
    else if (node.binding.isAssetRef()) {
        buildAssetEditor(node, row, slot, index);
    }
    else {
        return false;
    }

    if (node.kind == PropertyNode::Kind::Value && node.bEditable && node.binding.canMutateContainer() &&
        (node.binding.slot().elementIndex >= 0 || node.binding.slot().mapKey.has_value())) {
        appendRemoveButton(node, row, index);
    }
    _editors.push_back(std::move(slot));
    return true;
}

void EditorAutoPropertySection::construct()
{
    auto rows = ui::column("AutoPropertyRows").setSpacing(editor_density::kRowSpacing);
    FGroupNester groups(rows, _groupExpanded);
    for (const PropertyNode& node : _graph.getNodes()) {
        if (!node.bVisible) {
            continue;
        }
        groups.open(node.group);
        auto row = ui::row("PropertyRow_" + node.name).setSpacing(editor_density::kControlSpacing);
        row.child(ui::text("PropertyLabel_" + node.name)
                      .setText(node.displayName)
                      .setStyleKey("text.muted")
                      .setVAlign(EWidgetAlignV::Center),
                  node.binding.isAssetRef() ? labelTopSlot() : labelColumnSlot());
        EditorSlot slot;
        slot.node = &node;
        if (!buildEditor(node, row, slot)) {
            continue; // no editor for this value type: no row, as before
        }
        groups.attach(std::move(row));
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
    const std::string fingerprint = structureFingerprint();
    if (!_structureFingerprint.empty() && fingerprint != _structureFingerprint) {
        rebuildRows();
    }
    _structureFingerprint = fingerprint;
    UIElement* focused = tree.getFocused();
    for (EditorSlot& slot : _editors) {
        if (slot.node && slot.pull) {
            slot.pull(focused);
        }
    }
}

// Member template instantiations used only from this translation unit.
template void EditorAutoPropertySection::commitValue<float>(size_t, const float&, std::string);
template void EditorAutoPropertySection::commitValue<int64_t>(size_t, const int64_t&, std::string);
template void EditorAutoPropertySection::commitValue<bool>(size_t, const bool&, std::string);
template void EditorAutoPropertySection::commitValue<std::string>(size_t, const std::string&, std::string);
template void EditorAutoPropertySection::commitKnownBefore<glm::vec2>(size_t, const glm::vec2&, std::string, std::vector<glm::vec2>);
template void EditorAutoPropertySection::commitKnownBefore<glm::vec3>(size_t, const glm::vec3&, std::string, std::vector<glm::vec3>);
template void EditorAutoPropertySection::commitKnownBefore<glm::vec4>(size_t, const glm::vec4&, std::string, std::vector<glm::vec4>);

} // namespace ya
