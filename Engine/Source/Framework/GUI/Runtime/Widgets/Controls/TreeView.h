#pragma once

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <memory>
#include <limits>
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
/// selection/hover highlight) and hit-tests the same flatten at input. When
/// hosted inside a UIScrollViewport it paints only the scrolled visible window
/// (uniform row stride); there are no per-row child widgets.
struct YA_GUI_API UITreeView : public UIElement, public UIStyledWidget<UITreeView, FTreeViewStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FTreeViewStyle)

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
    /// Explicit intrinsic extent used only when measured without a parent
    /// edge (for example a designer preview root).
    glm::vec2 _intrinsicSize = {0.0f, 0.0f};
    /// Width of the expand/collapse arrow button (also its hover/hit area).
    float     _arrowWidth    = 22.0f;

    /// Fired after a row is selected (with the node id).
    std::function<void(const std::string& id)> _onSelectionChanged;
    /// Fired after a node is expanded/collapsed (diagnostics + host sync).
    std::function<void(const std::string& id, bool bExpanded)> _onToggleExpanded;

    /// Visible row count under the current expand/filter state (dump /
    /// scenario assertions).
    [[nodiscard]] int getVisibleRowCount() const { return static_cast<int>(flattenVisible().size()); }
    /// Rows painted in the last paint pass (full count when not scroll-hosted).
    [[nodiscard]] size_t getPaintedRowCount() const { return _lastPaintedRowCount; }

    void setReorderable(bool value) { _bReorderable = value; }
    void setIntrinsicSize(glm::vec2 value)
    {
        value = glm::max(value, glm::vec2(0.0f));
        if (_intrinsicSize == value) {
            return;
        }
        _intrinsicSize = value;
        invalidateProperty(EUIPropertyImpact::Layout);
    }
    void setOnReorderHandler(std::function<void(const std::string& fromId, const std::string& toId, int mode)> handler)
    {
        _onReorder = std::move(handler);
    }

    // === Editing (editor-parity P5) ===
    /// When true a press on a row (not the arrow) starts a tree drag
    /// session; dropping onto another row fires _onReorder.
    bool _bReorderable = false;
    /// Fired when a dragged node is dropped: mode 0 = before the target,
    /// 1 = into the target (as its child), 2 = after the target. The host
    /// rebuilds the ReactiveList (the widget never mutates the data).
    std::function<void(const std::string& fromId, const std::string& toId, int mode)> _onReorder;
    /// Fired on a right-button press over a row; the host opens its own
    /// context menu (the widget never owns menus).
    std::function<void(const std::string& nodeId, const glm::vec2& logicalPoint)> _onContextMenu;
    /// Filter ref: only nodes whose id/label matches (or that have a
    /// matching descendant) stay visible; while a filter is active all
    /// matching chains are shown expanded.
    void bindFilter(std::shared_ptr<Reactive<std::string>> ref);

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override
    {
        node["control"] = {
            {"type", "treeView"},
            {"visibleRows", getVisibleRowCount()},
            {"paintedRows", _lastPaintedRowCount},
            {"selected", _selectedId ? _selectedId->value() : std::string{}},
        };
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] glm::vec2 computeIntrinsicSize() const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerLeave() override;
    void clearTransientInputState() override;

  private:
    friend struct FTreeViewReorderDragBehavior;
    friend struct FTreeViewReorderDropBehavior;
    struct VisibleRow
    {
        const FNode* node  = nullptr;
        int          depth = 0;
    };

    /// Payload prefix carried by reorder drag sessions from this tree.
    static constexpr const char* kReorderPayloadPrefix = "tree-node:";
    /// Hard cap on data depth (defensive: cyclic node data must never
    /// recurse forever in flatten / filter walks).
    static constexpr int kMaxDepth = 64;

    /// Flatten visible rows under the current expand state. Reads the expand
    /// refs via get() so the paint walk records them as dependencies; when
    /// called outside the paint walk (input / measure) those reads are no-ops.
    [[nodiscard]] std::vector<VisibleRow> flattenVisible() const;
    void flattenNode(const FNode& node, int depth, std::vector<VisibleRow>& rows) const;
    void collectNodeIds(const FNode& node, std::unordered_set<std::string>& ids) const;
    void reconcileKeyedState() const;
    /// Whether `node` or any of its descendants matches the active filter.
    [[nodiscard]] bool matchesFilter(const FNode& node) const;
    [[nodiscard]] bool matchesFilterDescendants(const FNode& node, const std::string& filter, int depth) const;
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
    /// One-shot filter expansion: expands every matching chain exactly once
    /// when the filter text changes (kept in _lastFilterApplied); manual
    /// toggles stay authoritative afterwards.
    void applyFilterExpansion();
    void expandMatchingChain(const FNode& node, const std::string& filter);
    /// Drop position for `point`: fills `outRowIndex` / `outMode` (0 before,
    /// 1 into, 2 after). Returns false when not over a row.
    [[nodiscard]] bool dropPosition(const glm::vec2& point, int& outRowIndex, int& outMode) const;

    std::shared_ptr<ReactiveList<FNode>>     _roots;
    mutable uint64_t _observedRootsRevision = std::numeric_limits<uint64_t>::max();
    std::shared_ptr<Reactive<std::string>>   _selectedId;
    std::shared_ptr<Reactive<std::string>>   _filterBinding;
    std::string _lastFilterApplied;
    std::unordered_map<std::string, std::shared_ptr<Reactive<bool>>> _expanded;
    int _hoveredRow = -1;
    /// Node id whose arrow button is hovered (empty when none). Drives the
    /// arrow hover highlight.
    std::string _hoveredArrowId;
    /// Reorder drag state: the row the press started on, the press point
    /// (threshold), and the current drop row/mode while dragging.
    std::string _pressRowId;
    glm::vec2   _pressPoint{0.0f, 0.0f};
    bool        _bPressArmed = false;
    int _dropRowIndex = -1;
    int         _dropMode     = 0;
    mutable size_t _lastPaintedRowCount = 0;
};

} // namespace ya
