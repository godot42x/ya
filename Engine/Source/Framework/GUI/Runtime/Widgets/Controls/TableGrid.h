#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace ya
{

/// Data-driven table/grid (editor grid panels: debug image grids, skybox
/// previews, two-column settings tables). Built on the same reactive
/// data-source contract as UITreeView: bindData(ReactiveList<FTableRow>) +
/// bindSelection(Reactive<string> row id). Paints rows flat with header /
/// selected / hover states and vector-drawn separators.
///
/// Cells may hold EITHER text from the row data OR an arbitrary child
/// widget: attach a widget and set its UITableSlot cell (row/col) — the
/// table layout arranges it into the cell rect and the widget paints
/// itself on top of the cell (the row text for that cell is suppressed).
struct YA_GUI_API UITableGrid : public UIElement, public UIStyledWidget<UITableGrid, FTableGridStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FTableGridStyle)

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

    // === Selection (Reactive<string>, row id; empty = none) ===
    void bindSelection(std::shared_ptr<Reactive<std::string>> selectedId);
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> getSelection() const { return _selectedId; }

    // === Cell widgets (arbitrary UIElement in a cell) ===
    /// Configure the column count / row height of the cell layout.
    void setColumnCount(int count) { _tableLayout.setColumnCount(count); }
    void setColumnWidth(int column, float width) { _tableLayout.setColumnWidth(column, width); }
    void setRowHeight(float height);
    void setIntrinsicSize(glm::vec2 value)
    {
        value = glm::max(value, glm::vec2(0.0f));
        if (_intrinsicSize == value) {
            return;
        }
        _intrinsicSize = value;
        invalidateProperty(EUIPropertyImpact::Layout);
    }
    [[nodiscard]] UITableSlot* getCellSlot(const UIElement& child)
    {
        if (UISlot* edge = getSlotForChild(child)) {
            return edge->as<UITableSlot>();
        }
        return nullptr;
    }

    // === Visuals ===
    /// Column widths; 0 = stretch (shares the remaining width).
    std::vector<float> _columnWidths;
    float              _rowHeight = 22.0f;
    /// Explicit intrinsic extent used only when measured without a parent
    /// edge (for example a designer preview root).
    glm::vec2          _intrinsicSize = {0.0f, 0.0f};
    uint32_t           _fontSize  = 13;
    /// When true the first data row is drawn with the header text color.
    bool               _bHeaderRow = true;

    /// Fired after a row is selected (with the row index).
    std::function<void(int rowIndex)> _onSelectionChanged;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override {
        node["control"] = {{"type", "tableGrid"}, {"selected", _selectedId ? _selectedId->value() : std::string{}}};
    }
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] glm::vec2 computeIntrinsicSize() const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    [[nodiscard]] ECursorType getCursor() const override;
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
    /// Column to the left of a vertical splitter under `point`, or -1.
    [[nodiscard]] int hitColumnSplitter(const glm::vec2& point) const;
    [[nodiscard]] bool hitRowSplitter(const glm::vec2& point) const;
    void materializeStretchColumns();

    enum class EResize : uint8_t
    {
        None,
        Column,
        Row,
    };

    UITableLayout _tableLayout;
    std::shared_ptr<ReactiveList<FTableRow>> _rows;
    std::shared_ptr<Reactive<std::string>>  _selectedId;
    uint64_t                               _observedRowsRevision = std::numeric_limits<uint64_t>::max();
    int _hoveredRow = -1;
    EResize   _resize            = EResize::None;
    EResize   _hoverResize       = EResize::None;
    int       _resizeColumn      = -1;
    glm::vec2 _resizeStart{0.0f, 0.0f};
    float     _resizeStartValue  = 0.0f;
};

} // namespace ya
