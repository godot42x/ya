#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Binding/UndoStack.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace ui
{
class UIContainerWidgetBuilder;
}

struct UIElement;
struct UIDragFloat;
class UndoStack;
struct WidgetTree;

/// Generic retained editor for the scalar/vector properties in a PropertyGraph.
/// It owns controls, but not selection or component lifetime. Undo closures
/// capture `PropertyHandle` copies; the stack type still has no ECS pointers.
///
/// Row construction and per-frame refresh share one dispatch: the builder that
/// materializes an editor kind also installs that row's `pull`, so adding an
/// editor kind means one builder function, not parallel switches in
/// construct() and sync().
class EditorAutoPropertySection final : public UICompoundWidget
{
  public:
    /// Who records undo for this section's writes. By default the section
    /// pushes one PropertyHandle command per edit onto `undo`. An owner that
    /// snapshots its whole document instead installs a sink: the section still
    /// writes the value, then reports the edit and each drag gesture, and never
    /// pushes to `undo`.
    struct FEditCommitSink
    {
        std::function<void(const std::string& label, const std::string& mergeKey)> commit;
        std::function<void()> beginGesture;
        std::function<void()> endGesture;
    };

    EditorAutoPropertySection(std::string name,
                              PropertyGraph graph,
                              UndoStack* undo = nullptr,
                              std::string mergeIdentity = {},
                              EditorAssetPickerCallback assetPicker = {},
                              EditorRevealAssetCallback revealAsset = {});

    void sync(WidgetTree& tree);
    void setOnMutated(std::function<void()> fn) { _onMutated = std::move(fn); }
    /// Install before the section is attached (rows bind gestures on construct).
    void setEditCommitSink(FEditCommitSink sink) { _commitSink = std::move(sink); }

  protected:
    void construct() override;

  private:
    /// One editor row. Widgets live in the pull / push closures; container
    /// rows (add/clear) carry no pull because they hold no live value.
    struct EditorSlot
    {
        const PropertyNode* node = nullptr;
        std::function<void(UIElement* focused)> pull;
    };

    PropertyGraph _graph;
    UndoStack* _undo = nullptr;
    std::string _mergeIdentity;
    EditorAssetPickerCallback _assetPicker;
    EditorRevealAssetCallback _revealAsset;
    std::function<void()> _onMutated;
    FEditCommitSink _commitSink;
    std::vector<EditorSlot> _editors;
    std::string _structureFingerprint;
    std::unordered_map<std::string, bool> _groupExpanded;

    // construct() schedules rows; one builder per editor kind carries the
    // detail. Builders receive the row's future _editors index and wire their
    // own undo push and per-frame pull.
    bool buildEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot);
    void buildContainerEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, size_t index);
    void buildColorEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    template <int N>
    void buildVecEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildScalarEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildIntegerEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildBoolEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildStringEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildEnumEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void buildAssetEditor(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, EditorSlot& slot, size_t index);
    void appendRemoveButton(const PropertyNode& node, ui::UIContainerWidgetBuilder& row, size_t index);

    // One undo idiom: snapshot -> write -> snapshot -> push. The codec selects
    // the PropertyHandle API per value kind (bool / enum / color / asset need
    // their own snapshot calls); the vec-axis drag reuses a known snapshot.
    template <typename Codec, typename V>
    void commitWithCodec(size_t index, const V& value, std::string mergeKey);
    template <typename Codec, typename V, typename S>
    void pushValueUndo(size_t index, const V& value, std::string mergeKey, std::vector<S> before);
    template <typename V>
    void commitValue(size_t index, const V& value, std::string mergeKey = {});
    template <typename V>
    void commitKnownBefore(size_t index, const V& value, std::string mergeKey, std::vector<V> before);
    void commitColor(size_t index, const glm::vec4& value);
    void commitEnumIndex(size_t index, int selectedIndex);
    void commitAssetPath(size_t editorIndex, const std::string& value);

    void bindDragMerge(UIDragFloat& drag);
    void pushUndo(FUndoCommand command);
    [[nodiscard]] std::string mergeKey(const PropertyNode& node, int axis = -1) const;
    /// Container sizes only: the structure signal behind rebuildRows().
    [[nodiscard]] std::string structureFingerprint() const;
    void rebuildRows();
};

} // namespace ya
