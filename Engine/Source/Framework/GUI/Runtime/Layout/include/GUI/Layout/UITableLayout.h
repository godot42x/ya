#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Layout/UILayoutBase.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace ya
{

/// Layout data carried by one UITableLayout parent-child edge: the cell
/// position of the child inside the table grid.
class YA_GUI_API UITableSlot final : public UISlot
{
public:
    UITableSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] int getRow() const { return _row; }
    [[nodiscard]] int getColumn() const { return _column; }

    void setCell(int row, int column);
    void apply(const struct FTableSlotArgs& args);
    void serialize(nlohmann::json& node) const override;
    void deserialize(const nlohmann::json& node) override;

private:
    int _row    = 0;
    int _column = 0;
};

struct FTableSlotArgs
{
    using SlotType = UITableSlot;

    int row    = 0;
    int column = 0;
};

/// Grid layout: children are placed into row/column cells. Columns are
/// fixed-width or stretch (width 0 = share the remaining width equally);
/// rows share one fixed row height. The smallest table layout the editor's
/// grid panels need (no span / no column resize UI — those are later steps).
class YA_GUI_API UITableLayout final : public UILayout
{
public:
    [[nodiscard]] int getColumnCount() const { return _columnCount; }
    [[nodiscard]] float getColumnWidth(int column) const;
    [[nodiscard]] float getRowHeight() const { return _rowHeight; }
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    [[nodiscard]] bool clipsChildren() const { return _bClipChildren; }

    void setColumnCount(int value);
    /// Width of one column; 0 (default) = stretch (shares the remaining
    /// width equally with the other stretch columns).
    void setColumnWidth(int column, float value);
    void setRowHeight(float value);
    void setPadding(glm::vec2 value);
    void setClipsChildren(bool value);

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

    /// Resolved column rects for the last arrange (row 0, content space).
    [[nodiscard]] const std::vector<Rect2D>& getColumnRects() const { return _columnRects; }

private:
    int              _columnCount  = 2;
    std::vector<float> _columnWidths; // 0 = stretch
    float            _rowHeight    = 24.0f;
    glm::vec2        _padding      = {0.0f, 0.0f};
    bool             _bClipChildren = false;
    mutable std::vector<Rect2D> _columnRects;
};

} // namespace ya
