#include "GUI/Widgets/Controls/TableGrid.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace ya
{

UITableGrid::UITableGrid(std::string name)
    : UIElement(std::move(name), "table")
{
    _hitFilter     = EWidgetHitFilter::Stop;
    _selectedId = std::make_shared<Reactive<std::string>>(std::string{});
    bindHostLayout(_tableLayout);
    _tableLayout.setColumnCount(1);
}

void UITableGrid::bindData(std::shared_ptr<ReactiveList<FTableRow>> rows)
{
    _rows = std::move(rows);
    _observedRowsRevision = std::numeric_limits<uint64_t>::max();
    _hoveredRow = -1;
    markLayoutDirty();
}

void UITableGrid::bindSelection(std::shared_ptr<Reactive<std::string>> selectedId)
{
    _selectedId = std::move(selectedId);
    _observedRowsRevision = std::numeric_limits<uint64_t>::max();
    markPaintDirty();
}

void UITableGrid::setRowHeight(float height)
{
    _rowHeight = std::max(1.0f, height);
    _tableLayout.setRowHeight(_rowHeight);
    markLayoutDirty();
}

std::unique_ptr<UISlot> UITableGrid::createSlotForChild(UIElement& child)
{
    return _tableLayout.createSlot(*this, child);
}

void UITableGrid::applyAssignedLayout(const Rect2D& rect)
{
    if (_rows && _rows->isKeyed() && _selectedId && _observedRowsRevision != _rows->revision()) {
        if (!_selectedId->value().empty() && _rows->indexOfKey(_selectedId->value()) >= _rows->size()) {
            _selectedId->set({});
        }
        _observedRowsRevision = _rows->revision();
        _hoveredRow = -1;
    }
    // Keep the layout's column widths in sync with the visual column widths.
    _tableLayout.setColumnCount(static_cast<int>(_columnWidths.empty() ? 1 : _columnWidths.size()));
    for (size_t col = 0; col < _columnWidths.size(); ++col) {
        if (_tableLayout.getColumnWidth(static_cast<int>(col)) != _columnWidths[col]) {
            _tableLayout.setColumnWidth(static_cast<int>(col), _columnWidths[col]);
        }
    }
    _tableLayout.setRowHeight(_rowHeight);
    UIElement::applyAssignedLayout(rect);
}

bool UITableGrid::cellHasWidget(int row, int col) const
{
    for (const UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const UISlot* edge = getSlotForChild(*child);
        const auto* slot = edge ? edge->as<UITableSlot>() : nullptr;
        if (slot && slot->getRow() == row && slot->getColumn() == col) {
            return true;
        }
    }
    return false;
}

std::vector<Rect2D> UITableGrid::columnRects() const
{
    std::vector<Rect2D> rects;
    const size_t colCount = _columnWidths.empty() ? 1 : _columnWidths.size();
    const float  contentW = _layoutRect.extent.x;

    float fixedSum = 0.0f;
    size_t stretchCount = 0;
    for (float w : _columnWidths) {
        if (w > 0.0f) {
            fixedSum += w;
        }
        else {
            ++stretchCount;
        }
    }
    const float stretchWidth = stretchCount > 0
                                   ? std::max(0.0f, (contentW - fixedSum) / static_cast<float>(stretchCount))
                                   : 0.0f;

    float cursorX = _layoutRect.pos.x;
    for (size_t col = 0; col < colCount; ++col) {
        const float w = (col < _columnWidths.size() && _columnWidths[col] > 0.0f)
                            ? _columnWidths[col]
                            : stretchWidth;
        rects.push_back(Rect2D{.pos = {cursorX, _layoutRect.pos.y}, .extent = {w, _rowHeight}});
        cursorX += w;
    }
    return rects;
}

int UITableGrid::hitRowIndex(const glm::vec2& point) const
{
    if (point.x < _layoutRect.pos.x || point.x > _layoutRect.pos.x + _layoutRect.extent.x) {
        return -1;
    }
    if (point.y < _layoutRect.pos.y) {
        return -1;
    }
    const int index = static_cast<int>((point.y - _layoutRect.pos.y) / _rowHeight);
    if (!_rows || index < 0 || index >= static_cast<int>(_rows->size())) {
        return -1;
    }
    return index;
}

int UITableGrid::hitColumnSplitter(const glm::vec2& point) const
{
    constexpr float kHit = 3.0f;
    if (point.y < _layoutRect.pos.y || point.y > _layoutRect.pos.y + _layoutRect.extent.y) {
        return -1;
    }
    const auto rects = columnRects();
    for (size_t col = 1; col < rects.size(); ++col) {
        if (std::abs(point.x - rects[col].pos.x) <= kHit) {
            return static_cast<int>(col) - 1;
        }
    }
    return -1;
}

bool UITableGrid::hitRowSplitter(const glm::vec2& point) const
{
    constexpr float kHit = 3.0f;
    if (point.x < _layoutRect.pos.x || point.x > _layoutRect.pos.x + _layoutRect.extent.x) {
        return false;
    }
    const size_t rowCount = _rows ? _rows->size() : 0;
    for (size_t row = 1; row <= rowCount; ++row) {
        const float y = _layoutRect.pos.y + static_cast<float>(row) * _rowHeight;
        if (std::abs(point.y - y) <= kHit) {
            return true;
        }
    }
    return false;
}

void UITableGrid::materializeStretchColumns()
{
    const auto rects = columnRects();
    if (_columnWidths.empty()) {
        _columnWidths.resize(rects.size(), 0.0f);
    }
    for (size_t col = 0; col < rects.size(); ++col) {
        if (col >= _columnWidths.size()) {
            _columnWidths.push_back(rects[col].extent.x);
            continue;
        }
        if (_columnWidths[col] <= 0.0f) {
            _columnWidths[col] = std::max(40.0f, rects[col].extent.x);
        }
    }
}

ECursorType UITableGrid::getCursor() const
{
    const EResize kind = _resize != EResize::None ? _resize : _hoverResize;
    if (kind == EResize::Column) {
        return ECursorType::ResizeEastWest;
    }
    if (kind == EResize::Row) {
        return ECursorType::ResizeNorthSouth;
    }
    return ECursorType::Arrow;
}

void UITableGrid::paintSelf(UIFrameBuilder& builder)
{
    // (Guardrail G1: the base paint template clips this widget's own rect.)
    if (_rows && _rows->isKeyed() && _selectedId && _observedRowsRevision != _rows->revision()) {
        if (!_selectedId->value().empty() && _rows->indexOfKey(_selectedId->value()) >= _rows->size()) {
            _selectedId->set({});
        }
        _observedRowsRevision = _rows->revision();
        _hoveredRow = -1;
    }
    const FTableGridStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.backgroundFill);

    // Resolve the selection first so the dependency is recorded even when no
    // font is available.
    const std::string selectedId = _selectedId ? _selectedId->get() : std::string{};

    auto font = builder.getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    const auto colRects = columnRects();
    if (!_rows || colRects.empty()) {
        return;
    }

    const size_t rowCount = _rows->size(ReactiveBase::EDirtyLevel::Layout);
    for (size_t row = 0; row < rowCount; ++row) {
        const FTableRow& data  = _rows->get(row, ReactiveBase::EDirtyLevel::Layout);
        const Rect2D     rowRect{
            .pos    = {_layoutRect.pos.x, _layoutRect.pos.y + static_cast<float>(row) * _rowHeight},
            .extent = {_layoutRect.extent.x, _rowHeight},
        };

        const FBrush& fill = resolveVisualFill(visualChrome(style),
                                               composeVisualFlags(static_cast<int>(row) == _hoveredRow,
                                                                  false,
                                                                  false,
                                                                  false,
                                                                  data.id == selectedId,
                                                                  false,
                                                                  false));
        if (fill.tintColor.a > 0.0f) {
            builder.addBrush(rowRect, fill);
        }

        if (font) {
            const glm::vec4 textColor = (_bHeaderRow && row == 0) ? style.headerTextColor : style.textColor;
            for (size_t col = 0; col < colRects.size() && col < data.cells.size(); ++col) {
                if (cellHasWidget(static_cast<int>(row), static_cast<int>(col))) {
                    continue; // a child widget paints this cell
                }
                Rect2D cell = colRects[col];
                cell.pos.y  = rowRect.pos.y;
                cell.extent.y = _rowHeight;
                cell.pos.x += 6.0f;
                cell.extent.x -= 12.0f;
                builder.addText(cell, data.cells[col], textColor, font,
                                EWidgetAlignH::Left, EWidgetAlignV::Center);
            }
        }
    }

    // Column separators (top to bottom of the widget's arranged height).
    const float bottomY = _layoutRect.pos.y + _layoutRect.extent.y;
    for (size_t col = 1; col < colRects.size(); ++col) {
        builder.addLine({colRects[col].pos.x, _layoutRect.pos.y},
                        {colRects[col].pos.x, bottomY},
                        style.gridColor, 1.0f);
    }
    // Row separators.
    for (size_t row = 1; row <= rowCount; ++row) {
        const float y = _layoutRect.pos.y + static_cast<float>(row) * _rowHeight;
        builder.addLine({_layoutRect.pos.x, y}, {_layoutRect.pos.x + _layoutRect.extent.x, y},
                        style.gridColor, 1.0f);
    }
}

bool UITableGrid::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::MouseMoved) {
        if (_resize == EResize::Column && _resizeColumn >= 0 &&
            static_cast<size_t>(_resizeColumn) < _columnWidths.size()) {
            const float next = std::max(40.0f, _resizeStartValue + (ctx.logicalPoint.x - _resizeStart.x));
            if (_columnWidths[static_cast<size_t>(_resizeColumn)] != next) {
                _columnWidths[static_cast<size_t>(_resizeColumn)] = next;
                markLayoutDirty();
            }
            return true;
        }
        if (_resize == EResize::Row) {
            const float next = std::max(16.0f, _resizeStartValue + (ctx.logicalPoint.y - _resizeStart.y));
            if (_rowHeight != next) {
                setRowHeight(next);
            }
            return true;
        }
        _hoverResize = hitColumnSplitter(ctx.logicalPoint) >= 0
                           ? EResize::Column
                           : (hitRowSplitter(ctx.logicalPoint) ? EResize::Row : EResize::None);
        const int row = hitRowIndex(ctx.logicalPoint);
        if (row != _hoveredRow) {
            _hoveredRow = row;
            markPaintDirty();
        }
        return row >= 0 || _hoverResize != EResize::None;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        const int splitterCol = hitColumnSplitter(ctx.logicalPoint);
        if (splitterCol >= 0) {
            materializeStretchColumns();
            _resize           = EResize::Column;
            _resizeColumn     = splitterCol;
            _resizeStart      = ctx.logicalPoint;
            _resizeStartValue = (static_cast<size_t>(splitterCol) < _columnWidths.size())
                                    ? _columnWidths[static_cast<size_t>(splitterCol)]
                                    : 40.0f;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        if (hitRowSplitter(ctx.logicalPoint)) {
            _resize           = EResize::Row;
            _resizeColumn     = -1;
            _resizeStart      = ctx.logicalPoint;
            _resizeStartValue = _rowHeight;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        const int row = hitRowIndex(ctx.logicalPoint);
        if (row < 0) {
            return false;
        }
        if (_selectedId && _rows && static_cast<size_t>(row) < _rows->size()) {
            _selectedId->set(_rows->get(static_cast<size_t>(row)).id);
        }
        if (_onSelectionChanged) {
            _onSelectionChanged(row);
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        if (_resize != EResize::None) {
            _resize       = EResize::None;
            _resizeColumn = -1;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        }
        return false;
    }

    return false;
}

void UITableGrid::onPointerLeave()
{
    // Pointer left the grid: clear the row hover (no further MouseMoved
    // will reach this widget), same contract as UITreeView/UIButton.
    if (_hoveredRow != -1) {
        _hoveredRow = -1;
        markPaintDirty();
    }
    _hoverResize = EResize::None;
}

void UITableGrid::clearTransientInputState()
{
    _hoveredRow   = -1;
    _resize       = EResize::None;
    _hoverResize  = EResize::None;
    _resizeColumn = -1;
}

glm::vec2 UITableGrid::computeDesiredSize() const
{
    // Parent-owned slots decide fixed versus auto sizing; report the current
    // data-driven content extent regardless of the edge state.
    const size_t rowCount = _rows ? _rows->size() : 0;
    return {320.0f, static_cast<float>(std::max<size_t>(rowCount, 1)) * _rowHeight};
}

glm::vec2 UITableGrid::computeIntrinsicSize() const
{
    return _intrinsicSize;
}

} // namespace ya
