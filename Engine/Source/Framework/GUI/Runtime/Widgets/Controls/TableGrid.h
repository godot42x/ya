#pragma once

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
/// bindSelection(Reactive<int> row index). Paints rows flat (no per-cell
/// child widgets, no virtualization), draws column/row separators through
/// the vector primitive API, and hit-tests the same flatten at input.
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

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    void clearTransientInputState() override;

private:
    /// Live row index under `point` (re-flattens), or -1.
    [[nodiscard]] int hitRowIndex(const glm::vec2& point) const;
    /// Resolved column rects for the current layout rect (content space).
    [[nodiscard]] std::vector<Rect2D> columnRects() const;

    std::shared_ptr<ReactiveList<FTableRow>> _rows;
    std::shared_ptr<Reactive<int>>           _selectedIndex;
    int _hoveredRow = -1;
};

} // namespace ya
