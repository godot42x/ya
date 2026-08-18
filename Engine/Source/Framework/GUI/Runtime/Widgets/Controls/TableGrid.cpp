#include "GUI/Widgets/Controls/TableGrid.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>
#include <climits>

namespace ya
{

UITableGrid::UITableGrid(std::string name)
    : UIElement(std::move(name))
{
    _hitFilter   = EWidgetHitFilter::Stop;
    _selectedIndex = std::make_shared<Reactive<int>>(-1);
}

void UITableGrid::bindData(std::shared_ptr<ReactiveList<FTableRow>> rows)
{
    _rows = std::move(rows);
    _hoveredRow = -1;
    markLayoutDirty();
}

void UITableGrid::bindSelection(std::shared_ptr<Reactive<int>> selectedIndex)
{
    _selectedIndex = std::move(selectedIndex);
    markPaintDirty();
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

void UITableGrid::paintSelf(UIFrameBuilder& builder)
{
    // Clip to our own rect: more rows than the arranged height must never
    // paint over what sits below (same contract as UITreeView).
    builder.pushClip(_layoutRect);

    builder.addSprite(_layoutRect, _backgroundColor, nullptr);

    // Resolve the selection first so the dependency is recorded even when no
    // font is available.
    const int selectedIndex = _selectedIndex ? _selectedIndex->get() : -1;

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    const auto colRects = columnRects();
    if (!_rows || colRects.empty()) {
        builder.popClip();
        return;
    }

    const size_t rowCount = _rows->size(ReactiveBase::EDirtyLevel::Layout);
    for (size_t row = 0; row < rowCount; ++row) {
        const FTableRow& data  = _rows->get(row, ReactiveBase::EDirtyLevel::Layout);
        const Rect2D     rowRect{
            .pos    = {_layoutRect.pos.x, _layoutRect.pos.y + static_cast<float>(row) * _rowHeight},
            .extent = {_layoutRect.extent.x, _rowHeight},
        };

        if (static_cast<int>(row) == selectedIndex) {
            builder.addSprite(rowRect, _selectedColor, nullptr);
        }
        else if (static_cast<int>(row) == _hoveredRow) {
            builder.addSprite(rowRect, _hoveredColor, nullptr);
        }

        if (font) {
            const glm::vec4 textColor = (_bHeaderRow && row == 0) ? _headerTextColor : _textColor;
            for (size_t col = 0; col < colRects.size() && col < data.cells.size(); ++col) {
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
                        _gridColor, 1.0f);
    }
    // Row separators.
    for (size_t row = 1; row <= rowCount; ++row) {
        const float y = _layoutRect.pos.y + static_cast<float>(row) * _rowHeight;
        builder.addLine({_layoutRect.pos.x, y}, {_layoutRect.pos.x + _layoutRect.extent.x, y},
                        _gridColor, 1.0f);
    }

    builder.popClip();
}

bool UITableGrid::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
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
        const int row = hitRowIndex(ctx.logicalPoint);
        if (row < 0) {
            return false;
        }
        if (_selectedIndex) {
            _selectedIndex->set(row);
        }
        if (_onSelectionChanged) {
            _onSelectionChanged(row);
        }
        return true;
    }

    return false;
}

void UITableGrid::clearTransientInputState()
{
    _hoveredRow = -1;
}

glm::vec2 UITableGrid::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    const float width = _size.x > 0.0f ? _size.x : 320.0f;
    const size_t rowCount = _rows ? _rows->size() : 0;
    return {width, static_cast<float>(std::max<size_t>(rowCount, 1)) * _rowHeight};
}

} // namespace ya
