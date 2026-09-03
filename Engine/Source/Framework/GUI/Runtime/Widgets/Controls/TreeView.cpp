#include "GUI/Widgets/Controls/TreeView.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Base.h"  

namespace ya
{

struct FTreeViewReorderDragBehavior final : public UIDragSourceBehavior
{
    FTreeViewReorderDragBehavior()
    {
        bCapturePointerOnPress = true;
        bBeginDragFromCapturedMove = true;
        setPressedState = [](UIElement& owner, bool bPressed)
        {
            if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
                tree->_bPressArmed = bPressed;
            }
        };
        operationFactory = [](UIElement& owner) -> UIDragDropOperationRef
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree || !tree->_bReorderable || tree->_pressRowId.empty()) {
                return nullptr;
            }
            auto operation = std::make_shared<UIDragDropOperation>();
            operation->typeId = "tree.reorder";
            operation->payload = std::string(UITreeView::kReorderPayloadPrefix) + tree->_pressRowId;
            operation->ghostLabel = tree->_pressRowId;
            tree->_pressRowId.clear();
            return operation;
        };
    }

    void onDetached(UIElement& owner) override
    {
        if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
            tree->_bPressArmed = false;
            tree->_pressRowId.clear();
        }
        UIDragSourceBehavior::onDetached(owner);
    }
};

struct FTreeViewReorderDropBehavior final : public UIDropTargetBehavior
{
    FTreeViewReorderDropBehavior()
    {
        acceptPayload = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree || !tree->_bReorderable || payload.rfind(UITreeView::kReorderPayloadPrefix, 0) != 0) {
                return false;
            }
            int rowIndex = -1;
            int mode     = 0;
            return tree->dropPosition(logicalPoint, rowIndex, mode);
        };
        handleDroppedPayload = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree) {
                return;
            }
            tree->_dropRowIndex = -1;
            tree->markPaintDirty();
            const std::string fromId = payload.substr(std::char_traits<char>::length(UITreeView::kReorderPayloadPrefix));
            int               rowIndex = -1;
            int               mode     = 0;
            if (!tree->dropPosition(logicalPoint, rowIndex, mode)) {
                return;
            }
            const auto rows = tree->flattenVisible();
            if (rowIndex >= static_cast<int>(rows.size())) {
                return;
            }
            if (tree->_onReorder) {
                tree->_onReorder(fromId, rows[static_cast<size_t>(rowIndex)].node->id, mode);
            }
        };
        setHighlightState = [](UIElement& owner, bool bHighlight)
        {
            if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
                if (bHighlight) {
                    tree->markPaintDirty();
                }
                else if (tree->_dropRowIndex >= 0) {
                    tree->_dropRowIndex = -1;
                    tree->markPaintDirty();
                }
            }
        };
        updateHoverState = [](UIElement& owner, const std::string&, const glm::vec2& logicalPoint)
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree) {
                return;
            }
            int dropRow = -1;
            int dropMode = 0;
            if (tree->dropPosition(logicalPoint, dropRow, dropMode) &&
                (dropRow != tree->_dropRowIndex || dropMode != tree->_dropMode)) {
                tree->_dropRowIndex = dropRow;
                tree->_dropMode     = dropMode;
                tree->markPaintDirty();
            }
        };
    }

    void onDetached(UIElement& owner) override
    {
        if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
            tree->_dropRowIndex = -1;
        }
        UIDropTargetBehavior::onDetached(owner);
    }
};

UITreeView::UITreeView(std::string name) : UIElement(std::move(name), "tree")
{
    _hitFilter  = EWidgetHitFilter::Stop;
    _selectedId = std::make_shared<Reactive<std::string>>();
    addBehavior(std::make_shared<FTreeViewReorderDragBehavior>());
    addBehavior(std::make_shared<FTreeViewReorderDropBehavior>());
}

void UITreeView::bindData(std::shared_ptr<ReactiveList<FNode>> roots)
{
    // The roots list is paint-collected (read via flattenVisible during paint);
    // its Layout granularity is decided at the read site, not here.
    _roots = std::move(roots);
    _expanded.clear();
    _hoveredRow = -1;
    _observedRootsRevision = std::numeric_limits<uint64_t>::max();
    markLayoutDirty();
}

void UITreeView::bindFilter(std::shared_ptr<Reactive<std::string>> ref)
{
    _filterBinding = std::move(ref);
    markLayoutDirty(); // visible-row set may change entirely
}

bool UITreeView::dropPosition(const glm::vec2& point, int& outRowIndex, int& outMode) const
{
    const int rowIndex = hitRowIndex(point);
    if (rowIndex < 0) {
        return false;
    }
    const float rowTop = _layoutRect.pos.y + static_cast<float>(rowIndex) * _rowHeight;
    const float third  = _rowHeight / 3.0f;
    const float localY = point.y - rowTop;
    if (localY < third) {
        outMode = 0; // before
    }
    else if (localY > _rowHeight - third) {
        outMode = 2; // after
    }
    else {
        outMode = 1; // into
    }
    outRowIndex = rowIndex;
    return true;
}

void UITreeView::bindSelection(std::shared_ptr<Reactive<std::string>> selectedId)
{
    _selectedId = std::move(selectedId); // paint-collected, Paint granularity (default)
    markPaintDirty();
}

std::shared_ptr<Reactive<bool>>& UITreeView::expandedRef(const std::string& id)
{
    auto& ref = _expanded[id];
    if (!ref) {
        ref = std::make_shared<Reactive<bool>>(false);
    }
    return ref;
}

void UITreeView::setExpanded(const std::string& id, bool expanded)
{
    expandedRef(id)->set(expanded);
    // The visible-row set is a paint attribute too: when the arranged rect
    // does not change (fixed-height tree), the Layout invalidation alone
    // would leave the incremental paint cache showing the old rows.
    markPaintDirty();
}

void UITreeView::toggleExpanded(const std::string& id)
{
    auto& ref = expandedRef(id);
    const bool bNext = !ref->value();
    ref->set(bNext);
    markPaintDirty(); // see setExpanded
    if (_onToggleExpanded) {
        _onToggleExpanded(id, bNext);
    }
}

bool UITreeView::isExpanded(const std::string& id) const
{
    const auto it = _expanded.find(id);
    // Expansion changes the visible-row count -> Layout granularity.
    return it != _expanded.end() ? it->second->get(ReactiveBase::EDirtyLevel::Layout) : false;
}

std::vector<UITreeView::VisibleRow> UITreeView::flattenVisible() const
{
    std::vector<VisibleRow> rows;
    if (!_roots) {
        return rows;
    }
    reconcileKeyedState();
    // A filter-text change must expand matching chains before ANY structural
    // consumer (layout, paint, hit test, diagnostics) reads the visible rows.
    // If this stayed paint-only, the first layout/input pass after a binding
    // update would still see the stale collapsed structure.
    const_cast<UITreeView*>(this)->applyFilterExpansion();
    // A data-source mutation (push/removeAt/clear) changes the visible-row
    // count and therefore this widget's desired size: Layout granularity.
    const size_t count = _roots->size(ReactiveBase::EDirtyLevel::Layout);
    for (size_t i = 0; i < count; ++i) {
        flattenNode(_roots->get(i, ReactiveBase::EDirtyLevel::Layout), 0, rows);
    }
    return rows;
}

void UITreeView::collectNodeIds(const FNode& node, std::unordered_set<std::string>& ids) const
{
    if (node.id.empty() || !ids.insert(node.id).second) {
        return;
    }
    for (const FNode& child : node.children) {
        collectNodeIds(child, ids);
    }
}

void UITreeView::reconcileKeyedState() const
{
    if (!_roots || !_roots->isKeyed() || _observedRootsRevision == _roots->revision()) {
        return;
    }
    _observedRootsRevision = _roots->revision();
    std::unordered_set<std::string> liveIds;
    for (size_t i = 0; i < _roots->size(); ++i) {
        collectNodeIds(_roots->get(i), liveIds);
    }
    auto& expanded = const_cast<UITreeView*>(this)->_expanded;
    for (auto it = expanded.begin(); it != expanded.end();) {
        if (!liveIds.contains(it->first)) it = expanded.erase(it);
        else ++it;
    }
}

bool UITreeView::matchesFilter(const FNode& node) const
{
    if (!_filterBinding) {
        return true;
    }
    const std::string& filter = _filterBinding->get(ReactiveBase::EDirtyLevel::Layout);
    if (filter.empty()) {
        return true;
    }
    if (node.id.find(filter) != std::string::npos ||
        node.label.find(filter) != std::string::npos) {
        return true;
    }
    return matchesFilterDescendants(node, filter, 0);
}

bool UITreeView::matchesFilterDescendants(const FNode& node, const std::string& filter, int depth) const
{
    if (depth > kMaxDepth) {
        return false; // defensive: cyclic data must never recurse forever
    }
    for (const FNode& child : node.children) {
        if (child.id.find(filter) != std::string::npos ||
            child.label.find(filter) != std::string::npos) {
            return true;
        }
        if (matchesFilterDescendants(child, filter, depth + 1)) {
            return true;
        }
    }
    return false;
}

void UITreeView::flattenNode(const FNode& node, int depth, std::vector<VisibleRow>& rows) const
{
    if (depth > kMaxDepth) {
        return; // defensive: cyclic data must never recurse forever
    }
    // A filter hides every node outside the matching chains (a node shows
    // only when it matches or one of its descendants does).
    if (_filterBinding && !_filterBinding->get(ReactiveBase::EDirtyLevel::Layout).empty() && !matchesFilter(node)) {
        return;
    }
    rows.push_back({&node, depth});
    // Expansion always honors the per-node state. Filtering expands the
    // matching chains ONCE when the filter text changes (applyFilterExpansion),
    // then the user's manual collapse/expand works normally — a filter must
    // never freeze the tree in a forced-expanded state.
    if (isExpanded(node.id)) {
        for (const FNode& child : node.children) {
            if (!_filterBinding || matchesFilter(child)) {
                flattenNode(child, depth + 1, rows);
            }
        }
    }
}

void UITreeView::applyFilterExpansion()
{
    if (!_filterBinding || !_roots) {
        _lastFilterApplied.clear();
        return;
    }
    const std::string current = _filterBinding->get(ReactiveBase::EDirtyLevel::Layout);
    if (current == _lastFilterApplied) {
        return;
    }
    _lastFilterApplied = current;
    if (current.empty()) {
        return; // clearing the filter never collapses anything
    }
    // One-shot: expand every matching chain so the user sees the results.
    const size_t count = _roots->size(ReactiveBase::EDirtyLevel::Layout);
    for (size_t i = 0; i < count; ++i) {
        expandMatchingChain(_roots->get(i, ReactiveBase::EDirtyLevel::Layout), current);
    }
}

void UITreeView::expandMatchingChain(const FNode& node, const std::string& filter)
{
    if (!matchesFilter(node)) {
        return;
    }
    expandedRef(node.id)->set(true);
    for (const FNode& child : node.children) {
        expandMatchingChain(child, filter);
    }
}

int UITreeView::hitRowIndex(const glm::vec2& point) const
{
    if (point.x < _layoutRect.pos.x || point.x > _layoutRect.pos.x + _layoutRect.extent.x) {
        return -1;
    }
    if (point.y < _layoutRect.pos.y) {
        return -1;
    }
    const int index = static_cast<int>((point.y - _layoutRect.pos.y) / _rowHeight);
    const auto rows = flattenVisible();
    if (index < 0 || index >= static_cast<int>(rows.size())) {
        return -1;
    }
    return index;
}

bool UITreeView::onArrow(const glm::vec2& point, const VisibleRow& row) const
{
    // The row itself was already resolved by hitRowIndex (y), so only the
    // horizontal band of the arrow button is tested here. The band extends
    // a few pixels beyond the drawn button so a slightly-off click still
    // lands — a 22px button is hard to hit precisely on a real display.
    constexpr float kHitSlack = 4.0f;
    const float x0 = _layoutRect.pos.x + static_cast<float>(row.depth) * _indentWidth;
    return point.x >= x0 - kHitSlack && point.x <= x0 + _arrowWidth + kHitSlack;
}

Rect2D UITreeView::arrowButtonRect(float x, float rowTopY) const
{
    // A compact button box vertically inset from the row, so the hover
    // highlight reads as a clickable button rather than a full-row strip.
    // Paint and hit test share this geometry.
    const float inset = 2.0f;
    return Rect2D{
        .pos    = {x, rowTopY + inset},
        .extent = {_arrowWidth, _rowHeight - inset * 2.0f},
    };
}

void UITreeView::paintSelf(UIFrameBuilder& builder)
{
    // (Guardrail G1: the base paint template now clips every widget to its
    // own rect, so the manual pushClip that used to guard overflow rows is
    // gone — the framework guarantees it.)

    const auto rows = flattenVisible();
    const FTreeViewStyle& style = resolvedStyle();
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);

    // Resolve the selection first so the dependency is recorded even when no
    // font is available (mirrors UIText::resolvedText ordering).
    const std::string selectedId = _selectedId ? _selectedId->get() : std::string{};

    for (size_t i = 0; i < rows.size(); ++i) {
        const VisibleRow& row = rows[i];
        const Rect2D      rowRect{
            .pos    = {_layoutRect.pos.x, _layoutRect.pos.y + static_cast<float>(i) * _rowHeight},
            .extent = {_layoutRect.extent.x, _rowHeight},
        };

        if (row.node->id == selectedId) {
            builder.addBrush(rowRect, style.selectedFill);
        }
        else if (static_cast<int>(i) == _hoveredRow) {
            builder.addBrush(rowRect, style.hoveredFill);
        }

        float x = rowRect.pos.x + static_cast<float>(row.depth) * _indentWidth;

        if (!row.node->children.empty()) {
            const bool     expanded  = isExpanded(row.node->id);
            const Rect2D   arrowRect = arrowButtonRect(x, rowRect.pos.y);
            if (row.node->id == _hoveredArrowId) {
                builder.addBrush(arrowRect, style.arrowHoveredFill);
            }
            if (font) {
                builder.addText(arrowRect, expanded ? "v" : ">", style.arrowColor, font,
                                EWidgetAlignH::Center, EWidgetAlignV::Center);
            }
            x += _arrowWidth;
        }

        if (font) {
            const Rect2D labelRect{
                .pos    = {x, rowRect.pos.y},
                .extent = {rowRect.pos.x + rowRect.extent.x - x, _rowHeight},
            };
            builder.addText(labelRect, row.node->label, style.textColor, font,
                            EWidgetAlignH::Left, EWidgetAlignV::Center);
        }
    }

    // Reorder drop highlight: a line at the insertion boundary (before /
    // after) or a full-row outline when dropping INTO the row.
    if (_dropRowIndex >= 0) {
        const float y = _layoutRect.pos.y + static_cast<float>(_dropRowIndex) * _rowHeight;
        if (_dropMode == 1) {
            builder.addRectOutline(Rect2D{.pos = {_layoutRect.pos.x, y},
                                          .extent = {_layoutRect.extent.x, _rowHeight}},
                                   style.dropIndicator, 2.0f);
        }
        else {
            const float lineY = y + (_dropMode == 0 ? 0.0f : _rowHeight);
            builder.addLine({_layoutRect.pos.x, lineY},
                            {_layoutRect.pos.x + _layoutRect.extent.x, lineY},
                            style.dropIndicator, 2.0f);
        }
    }
}

bool UITreeView::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::MouseMoved) {
        const int row = hitRowIndex(ctx.logicalPoint);
        if (row != _hoveredRow) {
            _hoveredRow = row;
            markPaintDirty();
        }
        // Arrow-button hover: only rows with children show an arrow, and
        // only the arrow band (not the whole row) counts as hovering it.
        std::string newArrowHover;
        if (row >= 0) {
            const auto        rows = flattenVisible();
            const VisibleRow& r    = rows[static_cast<size_t>(row)];
            if (!r.node->children.empty() && onArrow(ctx.logicalPoint, r)) {
                newArrowHover = r.node->id;
            }
        }
        if (newArrowHover != _hoveredArrowId) {
            _hoveredArrowId = std::move(newArrowHover);
            markPaintDirty();
        }
        (void)UIElement::handleInputEvent(event, ctx);
        return row >= 0;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        const int rowIndex = hitRowIndex(ctx.logicalPoint);
        if (rowIndex < 0) {
            return false;
        }
        const auto      rows = flattenVisible();
        const VisibleRow& row = rows[static_cast<size_t>(rowIndex)];

        // Right-button press: context menu (host owns the menu).
        const auto& pressEvent = static_cast<const MouseButtonPressedEvent&>(event);
        if (pressEvent.GetMouseButton() == EMouse::Right) {
            if (_onContextMenu) {
                _onContextMenu(row.node->id, ctx.logicalPoint);
            }
            return true;
        }

        if (!row.node->children.empty() && onArrow(ctx.logicalPoint, row)) {
            toggleExpanded(row.node->id);
        }
        else {
            if (_selectedId) {
                _selectedId->set(row.node->id);
            }
            if (_onSelectionChanged) {
                _onSelectionChanged(row.node->id);
            }
            if (_bReorderable) {
                _pressRowId  = row.node->id;
                (void)UIElement::handleInputEvent(event, ctx);
            }
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        _pressRowId.clear();
        (void)UIElement::handleInputEvent(event, ctx);
        return true;
    }

    return false;
}

void UITreeView::onPointerLeave()
{
    // The pointer left the tree entirely (hover moved to a sibling widget):
    // the per-row hover state must clear, since no further MouseMoved will
    // reach this widget. This is the same contract Button/RadioButton use.
    if (_hoveredRow != -1 || !_hoveredArrowId.empty()) {
        _hoveredRow = -1;
        _hoveredArrowId.clear();
        markPaintDirty();
    }
}

void UITreeView::clearTransientInputState()
{
    _hoveredRow = -1;
    _hoveredArrowId.clear();
    _bPressArmed = false;
    _pressRowId.clear();
    _dropRowIndex = -1;
    _dropMode = 0;
}

glm::vec2 UITreeView::computeDesiredSize() const
{
    // The parent-owned slot decides fixed versus auto sizing. The widget only
    // reports its current data-driven content extent.
    const auto rows = flattenVisible();
    return {0.0f, static_cast<float>(rows.size()) * _rowHeight};
}

glm::vec2 UITreeView::computeIntrinsicSize() const
{
    return _intrinsicSize;
}

} // namespace ya
