#pragma once

#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Data-driven tree view (hierarchy panel first brick): a tree list with
/// selection and expand/collapse, built on the reactive data-source contract.
///
/// Data flow (event-driven, Vue semantics):
///   - the data source is a ReactiveList<FNode> (root nodes); each node's
///     children is a static vector (dynamic child mutation is a later step);
///   - per-node expand state is a Reactive<bool> owned by this widget, set to
///     Layout granularity so a toggle re-runs measure/arrange;
///   - the selection is a Reactive<std::string> (node id), editable from the
///     host via bindSelection()/getSelection().
///
/// The widget flattens visible rows at paint (indent + arrow + label +
/// selection/hover highlight) and hit-tests the same flatten at input. No
/// virtualization, no per-row child widgets — the smallest closed loop.
struct YA_GUI_API UITreeView : public UIElement
{
    /// One tree node (value type owned by the data source). `children` is a
    /// static subtree for now; dynamic child mutation is a later milestone.
    struct FNode
    {
        std::string id;
        std::string label;
        std::vector<FNode> children;
    };

    explicit UITreeView(std::string name = "TreeView");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITreeView>; }

    // === Data source (ReactiveList<FNode> root nodes) ===
    /// Replace the root-node data source. Clears per-node expand state and
    /// invalidates layout (visible-row count may change).
    void bindData(std::shared_ptr<ReactiveList<FNode>> roots);

    // === Selection (Reactive<std::string>, node id) ===
    /// Bind an external selection ref (created internally when omitted). The
    /// ref is Paint-granularity: selection only re-paints, never re-lays out.
    void bindSelection(std::shared_ptr<Reactive<std::string>> selectedId);
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> getSelection() const { return _selectedId; }

    // === Expand state (per node id, owned by this widget) ===
    /// Set / toggle the expand state of `id` (creates the ref on demand).
    /// Layout-granularity: toggling re-runs measure/arrange.
    void setExpanded(const std::string& id, bool expanded);
    void toggleExpanded(const std::string& id);
    [[nodiscard]] bool isExpanded(const std::string& id) const;

    // === Visuals ===
    float     _rowHeight     = 24.0f;
    float     _indentWidth   = 16.0f;
    /// Width of the expand/collapse arrow button (also its hover/hit area).
    float     _arrowWidth    = 22.0f;
    uint32_t  _fontSize      = 14;
    glm::vec4 _textColor     = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 _selectedColor = {0.22f, 0.42f, 0.78f, 1.0f};
    glm::vec4 _hoveredColor  = {0.24f, 0.26f, 0.31f, 1.0f};
    glm::vec4 _arrowColor    = {0.60f, 0.65f, 0.70f, 1.0f};
    /// Background of the arrow button while the pointer hovers it, signaling
    /// that the arrow is clickable (it toggles the node's expand state).
    glm::vec4 _arrowHoveredColor = {0.32f, 0.36f, 0.44f, 1.0f};

    /// Fired after a row is selected (with the node id).
    std::function<void(const std::string& id)> _onSelectionChanged;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void clearTransientInputState() override;

  private:
    struct VisibleRow
    {
        const FNode* node  = nullptr;
        int          depth = 0;
    };

    /// Flatten visible rows under the current expand state. Reads the expand
    /// refs via get() so the paint walk records them as dependencies; when
    /// called outside the paint walk (input / measure) those reads are no-ops.
    [[nodiscard]] std::vector<VisibleRow> flattenVisible() const;
    void flattenNode(const FNode& node, int depth, std::vector<VisibleRow>& rows) const;
    /// Live row index under `point` (re-flattens, no cached state), or -1.
    [[nodiscard]] int hitRowIndex(const glm::vec2& point) const;
    /// Whether `point` is over the expand arrow button of `row` (the row is
    /// already resolved by hitRowIndex, so only the horizontal band is
    /// tested here).
    [[nodiscard]] bool onArrow(const glm::vec2& point, const VisibleRow& row) const;
    /// Button rect of the expand arrow at indent `x` / row top `rowTopY`
    /// (paint and hit test share this geometry so the hover highlight matches
    /// the clickable area exactly).
    [[nodiscard]] Rect2D arrowButtonRect(float x, float rowTopY) const;
    /// Expand-state ref for `id`, creating it (Layout granularity) on demand.
    [[nodiscard]] std::shared_ptr<Reactive<bool>>& expandedRef(const std::string& id);

    std::shared_ptr<ReactiveList<FNode>>     _roots;
    std::shared_ptr<Reactive<std::string>>   _selectedId;
    std::unordered_map<std::string, std::shared_ptr<Reactive<bool>>> _expanded;
    int _hoveredRow = -1;
    /// Node id whose arrow button is hovered (empty when none). Drives the
    /// arrow hover highlight.
    std::string _hoveredArrowId;
};

} // namespace ya
