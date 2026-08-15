#include "GUI/Widgets/Controls/TreeView.h"

#include "GUI/Resources/FontManager.h"
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
    _roots = std::move(roots);
    if (_roots) {
        // A data-source mutation (push/removeAt/clear) changes the visible-row
        // count, which changes this widget's desired size: Layout granularity.
        _roots->setDirtyLevel(ReactiveBase::EDirtyLevel::Layout);
    }
    _expanded.clear();
    _hoveredRow = -1;
    markLayoutDirty();
}

void UITreeView::bindSelection(std::shared_ptr<Reactive<std::string>> selectedId)
{
    _selectedId = std::move(selectedId);
    if (_selectedId) {
        _selectedId->setDirtyLevel(ReactiveBase::EDirtyLevel::Paint);
    }
    markPaintDirty();
}

std::shared_ptr<Reactive<bool>>& UITreeView::expandedRef(const std::string& id)
{
    auto& ref = _expanded[id];
    if (!ref) {
        ref = std::make_shared<Reactive<bool>>(false);
        ref->setDirtyLevel(ReactiveBase::EDirtyLevel::Layout);
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
    return it != _expanded.end() ? it->second->get() : false;
}

std::vector<UITreeView::VisibleRow> UITreeView::flattenVisible() const
{
    std::vector<VisibleRow> rows;
    if (!_roots) {
        return rows;
    }
    const size_t count = _roots->size();
    for (size_t i = 0; i < count; ++i) {
        flattenNode(_roots->get(i), 0, rows);
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
    const float x0 = _layoutRect.pos.x + static_cast<float>(row.depth) * _indentWidth;
    const float x1 = x0 + _arrowWidth;
    return point.x >= x0 && point.x <= x1;
}

void UITreeView::paintSelf(UIFrameBuilder& builder)
{
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
            const bool expanded = isExpanded(row.node->id);
            const Rect2D arrowRect{.pos = {x, rowRect.pos.y}, .extent = {_arrowWidth, _rowHeight}};
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
