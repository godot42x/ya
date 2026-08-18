#include "GUI/Widgets/Controls/TreeView.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

UITreeView::UITreeView(std::string name) : UIElement(std::move(name))
{
    _hitFilter  = EWidgetHitFilter::Stop;
    _selectedId = std::make_shared<Reactive<std::string>>();
}

void UITreeView::bindData(std::shared_ptr<ReactiveList<FNode>> roots)
{
    // The roots list is paint-collected (read via flattenVisible during paint);
    // its Layout granularity is decided at the read site, not here.
    _roots = std::move(roots);
    _expanded.clear();
    _hoveredRow = -1;
    markLayoutDirty();
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
}

void UITreeView::toggleExpanded(const std::string& id)
{
    auto& ref = expandedRef(id);
    ref->set(!ref->value());
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
    // A data-source mutation (push/removeAt/clear) changes the visible-row
    // count and therefore this widget's desired size: Layout granularity.
    const size_t count = _roots->size(ReactiveBase::EDirtyLevel::Layout);
    for (size_t i = 0; i < count; ++i) {
        flattenNode(_roots->get(i, ReactiveBase::EDirtyLevel::Layout), 0, rows);
    }
    return rows;
}

void UITreeView::flattenNode(const FNode& node, int depth, std::vector<VisibleRow>& rows) const
{
    rows.push_back({&node, depth});
    if (isExpanded(node.id)) {
        for (const FNode& child : node.children) {
            flattenNode(child, depth + 1, rows);
        }
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
    // horizontal band of the arrow button is tested here.
    const float x0 = _layoutRect.pos.x + static_cast<float>(row.depth) * _indentWidth;
    return point.x >= x0 && point.x <= x0 + _arrowWidth;
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
    auto       font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);

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
            builder.addSprite(rowRect, _selectedColor, nullptr);
        }
        else if (static_cast<int>(i) == _hoveredRow) {
            builder.addSprite(rowRect, _hoveredColor, nullptr);
        }

        float x = rowRect.pos.x + static_cast<float>(row.depth) * _indentWidth;

        if (!row.node->children.empty()) {
            const bool     expanded  = isExpanded(row.node->id);
            const Rect2D   arrowRect = arrowButtonRect(x, rowRect.pos.y);
            if (row.node->id == _hoveredArrowId) {
                // Hover highlight signals the arrow is a clickable button.
                builder.addSprite(arrowRect, _arrowHoveredColor, nullptr);
            }
            if (font) {
                builder.addText(arrowRect, expanded ? "v" : ">", _arrowColor, font,
                                EWidgetAlignH::Center, EWidgetAlignV::Center);
            }
            x += _arrowWidth;
        }

        if (font) {
            const Rect2D labelRect{
                .pos    = {x, rowRect.pos.y},
                .extent = {rowRect.pos.x + rowRect.extent.x - x, _rowHeight},
            };
            builder.addText(labelRect, row.node->label, _textColor, font,
                            EWidgetAlignH::Left, EWidgetAlignV::Center);
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
        return row >= 0;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        const int rowIndex = hitRowIndex(ctx.logicalPoint);
        if (rowIndex < 0) {
            return false;
        }
        const auto      rows = flattenVisible();
        const VisibleRow& row = rows[static_cast<size_t>(rowIndex)];
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
        }
        return true;
    }

    return false;
}

void UITreeView::clearTransientInputState()
{
    _hoveredRow = -1;
    _hoveredArrowId.clear();
}

glm::vec2 UITreeView::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    const auto rows = flattenVisible();
    return {_size.x, static_cast<float>(rows.size()) * _rowHeight};
}

} // namespace ya
