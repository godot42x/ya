#include "GUI/Widgets/Controls/TreeView.h"

#include "GUI/Widgets/Controls/DisclosureChrome.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/StringMatch.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Base.h"

#include <algorithm>

namespace ya
{

FTreeReorderDragDropOp::~FTreeReorderDragDropOp() = default;

struct FTreeViewReorderDragDrop
{
    static void install(UITreeView& view)
    {
        auto drag = std::make_shared<UIDragSourceBehavior>();
        drag->bCapturePointerOnPress = true;
        drag->bBeginDragFromCapturedMove = true;
        drag->setPressedState = [](UIElement& owner, bool bPressed)
        {
            if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
                tree->_bPressArmed = bPressed;
            }
        };
        drag->operationFactory = [](UIElement& owner) -> UIDragDropOperationRef
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree || !tree->_bReorderable || tree->_pressRowId.empty()) {
                return nullptr;
            }
            auto operation = FTreeReorderDragDropOp::make(tree->_pressRowId, tree->_pressRowId);
            tree->_pressRowId.clear();
            return operation;
        };
        drag->onOwnerDetached = [](UIElement& owner)
        {
            if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
                tree->_bPressArmed = false;
                tree->_pressRowId.clear();
            }
        };
        view.addBehavior(drag);

        auto drop = std::make_shared<UIDropTargetBehavior>();
        drop->canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree || !tree->_bReorderable || !operation.as<FTreeReorderDragDropOp>()) {
                return false;
            }
            int rowIndex = -1;
            int mode     = 0;
            return tree->dropPosition(logicalPoint, rowIndex, mode);
        };
        drop->handleDrop = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* tree = dynamic_cast<UITreeView*>(&owner);
            if (!tree) {
                return;
            }
            tree->_dropRowIndex = -1;
            tree->markPaintDirty();
            const auto* reorder = operation.as<FTreeReorderDragDropOp>();
            if (!reorder) {
                return;
            }
            const std::string fromId = reorder->rowId;
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
        drop->setHighlightState = [](UIElement& owner, bool bHighlight)
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
        drop->updateHover = [](UIElement& owner, const UIDragDropOperation&, const glm::vec2& logicalPoint)
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
        drop->onOwnerDetached = [](UIElement& owner)
        {
            if (auto* tree = dynamic_cast<UITreeView*>(&owner)) {
                tree->_dropRowIndex = -1;
            }
        };
        view.addBehavior(drop);
    }
};

UITreeView::UITreeView(std::string name) : UIElement(std::move(name), "tree")
{
    _hitFilter  = EWidgetHitFilter::Stop;
    _selectedId = std::make_shared<Reactive<std::string>>();
    FTreeViewReorderDragDrop::install(*this);
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

    // A generated row (engine-managed companion) is not a reorder target: the
    // host owns its order. It stays selectable, so only the drop is refused.
    const std::vector<VisibleRow> rows = flattenVisible();
    if (rowIndex >= static_cast<int>(rows.size())) {
        return false;
    }
    const FNode* target = rows[static_cast<size_t>(rowIndex)].node;
    if (target == nullptr || !target->bEnabled) {
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
    // Visible-row count changes desired height (Auto parents) and the
    // painted row set (fixed-height trees keep the same arranged rect).
    markLayoutDirty();
    markPaintDirty();
}

void UITreeView::toggleExpanded(const std::string& id)
{
    auto& ref = expandedRef(id);
    const bool bNext = !ref->value();
    ref->set(bNext);
    markLayoutDirty();
    markPaintDirty();
    if (_onToggleExpanded) {
        _onToggleExpanded(id, bNext);
    }
}

bool UITreeView::isExpanded(const std::string& id) const
{
    // Always create the Reactive so the first expand has a Layout dependent
    // (a missing id used to return false without registering anyone).
    return const_cast<UITreeView*>(this)->expandedRef(id)->get(ReactiveBase::EDirtyLevel::Layout);
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
    const EStringMatchCase mode =
        _bFilterCaseSensitive ? EStringMatchCase::Sensitive : EStringMatchCase::Ignore;
    if (stringContains(node.id, filter, mode) || stringContains(node.label, filter, mode)) {
        return true;
    }
    return matchesFilterDescendants(node, filter, 0);
}

bool UITreeView::matchesFilterDescendants(const FNode& node, const std::string& filter, int depth) const
{
    if (depth > kMaxDepth) {
        return false; // defensive: cyclic data must never recurse forever
    }
    const EStringMatchCase mode =
        _bFilterCaseSensitive ? EStringMatchCase::Sensitive : EStringMatchCase::Ignore;
    for (const FNode& child : node.children) {
        if (stringContains(child.id, filter, mode) || stringContains(child.label, filter, mode)) {
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
    constexpr float kHitSlack = 4.0f;
    const float x0 = _layoutRect.pos.x + static_cast<float>(row.depth) * _indentWidth;
    if (!showsDisclosureButton(_disclosure)) {
        if (brushHasIcon(row.node->icon)) {
            constexpr float kIcon = 14.0f;
            return point.x >= x0 - kHitSlack && point.x <= x0 + kIcon + kHitSlack;
        }
        return point.x >= x0 - kHitSlack && point.x <= x0 + 14.0f + kHitSlack;
    }
    return point.x >= x0 - kHitSlack && point.x <= x0 + _arrowWidth + kHitSlack;
}

FDisclosureLeading UITreeView::rowLeading(const Rect2D& rowRect,
                                          int           depth,
                                          bool          bHasIcon,
                                          float         packHeight) const
{
    Rect2D header = rowRect;
    const float indent = static_cast<float>(depth) * _indentWidth;
    header.pos.x += indent;
    header.extent.x = std::max(0.0f, header.extent.x - indent);
    return layoutDisclosureLeading(header,
                                   _arrowWidth,
                                   showsDisclosureButton(_disclosure),
                                   bHasIcon,
                                   14.0f,
                                   packHeight);
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

    size_t paintBegin = 0;
    size_t paintEnd   = rows.size();
    if (const UIElement* parent = getParent()) {
        if (const auto* scroll = dynamic_cast<const UIScrollViewport*>(parent)) {
            const float viewportExtent = scroll->getLayoutRect().extent.y;
            if (viewportExtent > 0.0f && !rows.empty()) {
                const FKeyedVisibleWindow window = computeKeyedVisibleWindow(rows.size(),
                                                                             _rowHeight,
                                                                             0.0f,
                                                                             viewportExtent,
                                                                             scroll->getScrollOffset(),
                                                                             2);
                paintBegin = window.first;
                paintEnd   = window.end();
            }
        }
    }
    _lastPaintedRowCount = paintEnd - paintBegin;

    for (size_t i = paintBegin; i < paintEnd; ++i) {
        const VisibleRow& row = rows[i];
        const Rect2D      rowRect{
            .pos    = {_layoutRect.pos.x, _layoutRect.pos.y + static_cast<float>(i) * _rowHeight},
            .extent = {_layoutRect.extent.x, _rowHeight},
        };

        const FBrush& fill = resolveVisualFill(visualChrome(style),
                                               composeVisualFlags(static_cast<int>(i) == _hoveredRow,
                                                                  false,
                                                                  false,
                                                                  false,
                                                                  row.node->id == selectedId,
                                                                  false,
                                                                  false));
        if (fill.tintColor.a > 0.0f) {
            builder.addBrush(rowRect, fill);
        }

        const bool bHasIcon = brushHasIcon(row.node->icon);
        const float packH   = font ? static_cast<float>(font->lineHeight) : 0.0f;
        const FDisclosureLeading leading =
            rowLeading(rowRect, row.depth, bHasIcon, packH);

        if (showsDisclosureButton(_disclosure) && !row.node->children.empty()) {
            paintDisclosureButton(builder,
                                  FDisclosurePaint{
                                      .buttonRect  = leading.button,
                                      .bExpanded   = isExpanded(row.node->id),
                                      .bHovered    = row.node->id == _hoveredArrowId,
                                      .color       = style.arrowColor,
                                      .hoveredFill = style.arrowHoveredFill,
                                      .spec        = _disclosure,
                                      .font        = font,
                                  });
        }

        if (bHasIcon) {
            builder.addBrush(leading.icon, row.node->icon);
        }

        if (font) {
            const glm::vec4 labelColor = row.node->bEnabled ? style.textColor : style.disabledTextColor;
            builder.addText(leading.title, row.node->label, labelColor, font,
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
        const auto& pressEvent = static_cast<const MouseButtonPressedEvent&>(event);
        if (pressEvent.GetMouseButton() == EMouse::Right) {
            const int rowIndex = hitRowIndex(ctx.logicalPoint);
            std::string nodeId;
            if (rowIndex >= 0) {
                const auto        rows = flattenVisible();
                const VisibleRow& row  = rows[static_cast<size_t>(rowIndex)];
                nodeId                 = row.node->id;
                if (_selectedId) {
                    _selectedId->set(nodeId);
                }
                if (_onSelectionChanged) {
                    _onSelectionChanged(nodeId);
                }
            }
            if (_onContextMenu) {
                _onContextMenu(nodeId, ctx.logicalPoint);
                return true;
            }
            return false;
        }

        const int rowIndex = hitRowIndex(ctx.logicalPoint);
        if (rowIndex < 0) {
            return false;
        }
        const auto        rows = flattenVisible();
        const VisibleRow& row  = rows[static_cast<size_t>(rowIndex)];

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
            if (_bReorderable && row.node->bEnabled) {
                _pressRowId = row.node->id;
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
