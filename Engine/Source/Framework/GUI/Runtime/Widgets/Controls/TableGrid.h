#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <string>
#include <vector>

namespace ya
{

/// Data-driven table/grid (editor grid panels: debug image grids, skybox
/// previews, two-column settings tables). Built on the same reactive
/// data-source contract as UITreeView: bindData(ReactiveList<FTableRow>) +
/// bindSelection(Reactive<int> row index). Paints rows flat with header /
/// selected / hover states and vector-drawn separators.
///
/// Cells may hold EITHER text from the row data OR an arbitrary child
/// widget: attach a widget and set its UITableSlot cell (row/col) — the
/// table layout arranges it into the cell rect and the widget paints
/// itself on top of the cell (the row text for that cell is suppressed).
struct YA_GUI_API UITableGrid : public UIElement
{
    /// One table row (value type owned by the data source).
    struct FTableRow
    {
        std::string              id;
        std::vector<std::string> cells;
    };

    explicit UITableGrid(std::string name = "TableGrid");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITableGrid>; }

    // === Data source ===
    /// Replace the row data source (invalidates layout: row count may change).
    void bindData(std::shared_ptr<ReactiveList<FTableRow>> rows);

    // === Selection (Reactive<int>, row index; -1 = none) ===
    void bindSelection(std::shared_ptr<Reactive<int>> selectedIndex);
    [[nodiscard]] std::shared_ptr<Reactive<int>> getSelection() const { return _selectedIndex; }

    // === Cell widgets (arbitrary UIElement in a cell) ===
    /// Configure the column count / row height of the cell layout.
    void setColumnCount(int count) { _tableLayout.setColumnCount(count); }
    void setColumnWidth(int column, float width) { _tableLayout.setColumnWidth(column, width); }
    void setRowHeight(float height);
    [[nodiscard]] UITableSlot* getCellSlot(const UIElement& child)
    {
        return dynamic_cast<UITableSlot*>(getSlotForChild(child));
    }

    // === Visuals ===
    /// Column widths; 0 = stretch (shares the remaining width).
    std::vector<float> _columnWidths;
    float              _rowHeight       = 22.0f;
    uint32_t           _fontSize        = 13;
    glm::vec4          _textColor       = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4          _headerTextColor = {0.62f, 0.66f, 0.72f, 1.0f};
    glm::vec4          _selectedColor   = {0.22f, 0.42f, 0.78f, 1.0f};
    glm::vec4          _hoveredColor    = {0.24f, 0.26f, 0.31f, 1.0f};
    glm::vec4          _gridColor       = {0.20f, 0.22f, 0.27f, 1.0f};
    glm::vec4          _backgroundColor = {0.12f, 0.13f, 0.16f, 1.0f};
    /// When true the first data row is drawn with the header text color.
    bool               _bHeaderRow = true;

    /// Fired after a row is selected (with the row index).
    std::function<void(int rowIndex)> _onSelectionChanged;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerLeave() override;
    void clearTransientInputState() override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

private:
    /// Live row index under `point` (re-flattens), or -1.
    [[nodiscard]] int hitRowIndex(const glm::vec2& point) const;
    /// Resolved column rects for the current layout rect (content space).
    [[nodiscard]] std::vector<Rect2D> columnRects() const;
    /// Whether a child widget occupies the given cell (suppresses the text).
    [[nodiscard]] bool cellHasWidget(int row, int col) const;

    UITableLayout _tableLayout;
    std::shared_ptr<ReactiveList<FTableRow>> _rows;
    std::shared_ptr<Reactive<int>>           _selectedIndex;
    int _hoveredRow = -1;
};

} // namespace ya
