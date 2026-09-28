#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Layout/UILayoutBase.h"
#include "GUI/Layout/UILayoutTypes.h"

#include <glm/glm.hpp>

#include <limits>
#include <memory>

namespace ya
{

struct FCanvasSlotArgs;

/// Canvas slot: the anchor-owning parent-child edge. The child's rect is
/// resolved from `anchorMin/anchorMax` against the parent rect, plus an offset
/// and optional min/max clamps. This replaces the historical
/// Child placement is authored on the parent-owned slot edge.
/// pattern: anchor intent lives on the parent->child edge, so a non-canvas
/// parent (which never reads this slot) cannot silently drop it.
///
/// Canvas is a LAYOUT, not a widget: any host that installs UICanvasLayout
/// (UICanvasPanel today) can carry these edges.
class YA_GUI_API UICanvasSlot final : public UISlot
{
public:
    UICanvasSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] const glm::vec2& getAnchorMin() const { return _anchorMin; }
    [[nodiscard]] const glm::vec2& getAnchorMax() const { return _anchorMax; }
    [[nodiscard]] const glm::vec2& getOffset() const { return _offset; }
    [[nodiscard]] const glm::vec2& getMinSize() const { return _minSize; }
    [[nodiscard]] const glm::vec2& getMaxSize() const { return _maxSize; }

    /// Per-edge insets, applied after the anchor rect is resolved. When all four
    /// edges are set the child stretches to the anchor rect minus the insets, so
    /// an inset layout does not need a hand-computed size.
    [[nodiscard]] const FMargin& getOffsets() const { return _offsets; }
    [[nodiscard]] EWidgetAlignH  getAlignmentH() const { return _alignmentH; }
    [[nodiscard]] EWidgetAlignV  getAlignmentV() const { return _alignmentV; }
    [[nodiscard]] EWidgetSizeMode getWidthSizeMode() const { return _widthSizeMode; }
    [[nodiscard]] EWidgetSizeMode getHeightSizeMode() const { return _heightSizeMode; }

    /// The point of the child that lands on the resolved anchor position, in
    /// normalized child space (0,0 = top-left, 0.5,0.5 = centre). Lets a child be
    /// centred or right/bottom-anchored without recomputing its position.
    [[nodiscard]] const glm::vec2& getPivot() const { return _pivot; }
    /// Size the child would like to be when its size mode is Auto on that axis.
    /// A zero component means "ask the child" (computeDesiredSize()).
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }
    /// Explicit fixed size for non-stretch, non-auto axes. This keeps canvas
    /// edge-owned size intent out of the child geometry state.
    [[nodiscard]] const glm::vec2& getFixedSize() const { return _fixedSize; }

    void setAnchorMin(glm::vec2 value);
    void setAnchorMax(glm::vec2 value);
    void setOffset(glm::vec2 value);
    void setMinSize(glm::vec2 value);
    void setMaxSize(glm::vec2 value);
    void setOffsets(FMargin value);
    void setAlignmentH(EWidgetAlignH value);
    void setAlignmentV(EWidgetAlignV value);
    void setWidthSizeMode(EWidgetSizeMode value);
    void setHeightSizeMode(EWidgetSizeMode value);
    void setPivot(glm::vec2 value);
    void setPreferredSize(glm::vec2 value);
    void setFixedSize(glm::vec2 value);
    void apply(const FCanvasSlotArgs& args);
    /// Exact slot state as args: `assign(toArgs())` is the identity. `apply`
    /// is the construct-time form and treats a zero size as "unset".
    [[nodiscard]] FCanvasSlotArgs toArgs() const;
    void assign(const FCanvasSlotArgs& args);
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;
    void serialize(nlohmann::json& node) const override;
    void deserialize(const nlohmann::json& node) override;
    [[nodiscard]] bool isAutoSizeActive() const override;

private:
    /// Spanning anchors or insets mean stretch. Leaving Auto on those axes
    /// measures a 0-desired child to 0px (invisible). Explicit Auto after
    /// `apply()` / `widthSizeMode(Auto)` is the SizeToContent escape hatch.
    void promoteStretchedAutoAxes();

    glm::vec2       _anchorMin = {0.0f, 0.0f};
    glm::vec2       _anchorMax = {0.0f, 0.0f};
    glm::vec2       _offset    = {0.0f, 0.0f};
    glm::vec2       _minSize   = {0.0f, 0.0f};
    glm::vec2       _maxSize   = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    FMargin         _offsets{};
    EWidgetAlignH   _alignmentH    = EWidgetAlignH::Left;
    EWidgetAlignV   _alignmentV    = EWidgetAlignV::Top;
    EWidgetSizeMode _widthSizeMode  = EWidgetSizeMode::Auto;
    EWidgetSizeMode _heightSizeMode = EWidgetSizeMode::Auto;
    glm::vec2       _pivot         = {0.0f, 0.0f};
    glm::vec2       _preferredSize = {0.0f, 0.0f};
    glm::vec2       _fixedSize     = {0.0f, 0.0f};
};

/// Construct-time canvas slot intent.
///
/// Aggregate default is Fixed/Fixed with zero `fixedSize` so `args.fixedSize =
/// {w,h}` (the historical attach payload) is honored without also setting size
/// modes. An empty aggregate is therefore a 0x0 child — invisible until
/// size/fill/anchors are set. DSL `ui::canvasSlot()` does **not** use this
/// default: the builder starts Auto/Auto, matching `UICanvasSlot` and a bare
/// `child(node)`.
struct FCanvasSlotArgs
{
    using SlotType = UICanvasSlot;

    glm::vec2 anchorMin = {0.0f, 0.0f};
    glm::vec2 anchorMax = {0.0f, 0.0f};
    glm::vec2 offset    = {0.0f, 0.0f};
    glm::vec2 minSize   = {0.0f, 0.0f};
    glm::vec2 maxSize   = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};

    /// Per-edge insets (all four edges => stretch to the anchor rect minus the
    /// insets). Kept separately from `offset` so both can coexist.
    FMargin         offsets{};
    EWidgetAlignH   alignmentH     = EWidgetAlignH::Left;
    EWidgetAlignV   alignmentV     = EWidgetAlignV::Top;
    EWidgetSizeMode widthSizeMode  = EWidgetSizeMode::Fixed;
    EWidgetSizeMode heightSizeMode = EWidgetSizeMode::Fixed;
    glm::vec2       pivot          = {0.0f, 0.0f};
    glm::vec2       preferredSize  = {0.0f, 0.0f};
    glm::vec2       fixedSize      = {0.0f, 0.0f};
};

/// Designer anchor presets. A point preset anchors and pivots the child on the
/// same parent point, so it sits flush in that corner / edge / the centre. A
/// stretch preset spans the axis with pivot 0 on it: a stretched axis takes
/// its position from the anchor span, and a pivot would shift it out.
enum class ECanvasAnchorPreset : uint8_t
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
    StretchHorizontal,
    StretchVertical,
    Fill,
};

/// `args` re-anchored to `preset`, keeping `size` on the axes that do not
/// stretch. Offset and insets are cleared and alignment is Left/Top (on a
/// point axis alignment works against the whole parent, not the anchor).
[[nodiscard]] YA_GUI_API FCanvasSlotArgs withCanvasAnchorPreset(FCanvasSlotArgs      args,
                                                                ECanvasAnchorPreset preset,
                                                                glm::vec2           size);

/// Canvas layout: children are positioned by anchor rects against the parent
/// content rect. This is the layout form of the historical "path-B" panel
/// behaviour; it is NOT bound to UICanvasPanel - any host may install it.
class YA_GUI_API UICanvasLayout final : public UILayout
{
public:
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    void setPadding(glm::vec2 value);

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

    /// Resolve one child rect from its canvas slot against the parent content
    /// rect. Anchor span/insets are the alignment area; child size is
    /// Auto (preferred else desired) > stretch-to-area > authored fixedSize.
    [[nodiscard]] static Rect2D resolveChildRect(const UIElement&    child,
                                                 const UICanvasSlot& slot,
                                                 const Rect2D&       contentRect);

private:
    glm::vec2 _padding = {0.0f, 0.0f};
};

} // namespace ya
