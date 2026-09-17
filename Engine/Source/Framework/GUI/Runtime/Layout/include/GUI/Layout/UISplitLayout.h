#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Layout/UILayoutBase.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace ya
{

enum class ESplitOrientation : uint8_t
{
    Vertical,   // divider runs vertically: panes sit side by side (left/right)
    Horizontal, // divider runs horizontally: panes stack (top/bottom)
};

/// Geometry policy for a two-pane split. Drag state stays on UISplitPane;
/// orientation, ratio, limits, padding and child arrangement live here.
/// `minFirst/SecondExtent` are pixel floors on the split axis. They raise a
/// too-small ratio; they do not shrink a larger ratio and they do not
/// rewrite the stored ratio. Extra space stays with the user's ratio.
class YA_GUI_API UISplitLayout final : public UILayout
{
public:
    [[nodiscard]] ESplitOrientation getOrientation() const { return _orientation; }
    [[nodiscard]] float getSplitRatio() const { return _splitRatio; }
    [[nodiscard]] float getMinFirstExtent() const { return _minFirstExtent; }
    [[nodiscard]] float getMinSecondExtent() const { return _minSecondExtent; }
    [[nodiscard]] float getDividerThickness() const { return _dividerThickness; }
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    [[nodiscard]] const Rect2D& getContentRect() const { return _contentRect; }
    [[nodiscard]] Rect2D getDividerRect() const;
    [[nodiscard]] float axisCoordinate(const glm::vec2& point) const;

    void setOrientation(ESplitOrientation value);
    void setSplitRatio(float value);
    void setMinFirstExtent(float value);
    void setMinSecondExtent(float value);
    void setDividerThickness(float value);
    void setPadding(glm::vec2 value);

    /// Both panes get their rect from the split, so a pane's child intent is
    /// carried by a content slot (fill by default).
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    void clampRatio() const;
    [[nodiscard]] float axisExtent(const Rect2D& rect) const;
    [[nodiscard]] float axisPosition(const glm::vec2& point) const;

    ESplitOrientation _orientation = ESplitOrientation::Vertical;
    mutable float      _splitRatio = 0.5f;
    float              _minFirstExtent = 00.0f;
    float              _minSecondExtent = 00.0f;
    float              _dividerThickness = 4.0f;
    glm::vec2          _padding = {0.0f, 0.0f};
    mutable Rect2D     _contentRect{};
};

} // namespace ya
