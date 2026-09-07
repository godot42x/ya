#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Widgets/UIElement.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>

namespace ya
{

struct WidgetTree;

/// Horizontal alignment inside a parent-owned canvas / overlay slot, and for
/// painted text. Lives with layout slots rather than the widget kernel.
enum class EWidgetAlignH : uint8_t
{
    Left,
    Center,
    Right,
};

/// Vertical alignment inside a parent-owned canvas / overlay slot, and for
/// painted text.
enum class EWidgetAlignV : uint8_t
{
    Top,
    Center,
    Bottom,
};

enum class EWidgetBoxLayout : uint8_t
{
    Horizontal,
    Vertical,
};

/// Main-axis arrangement of a box container (Stack role, gui-app-bootstrap
/// Phase 2): where the packed children sit when they do not fill the content
/// extent.
enum class EWidgetMainAxisAlignment : uint8_t
{
    Start,  // children packed at the content start (default)
    Center, // children centered along the main axis
    End,    // children packed at the content end
};

/// How a child resolves its own size on an axis when its rect is computed.
/// Defined here (not in UILayoutIntent.h) because UILayout needs it and
/// UILayoutIntent includes UILayout.
enum class EWidgetSizeMode : uint8_t
{
    Fixed, // honor the slot's authored size (fixedSize / preferredSize)
    Auto,  // resolve from computeDesiredSize() (SizeToContent)
};

/// Parent-owned child edge. A slot exists while its child belongs to its
/// visual parent; WidgetTree destroys the old edge before reparent/detach and
/// creates a new one under the destination parent.
class YA_GUI_API UISlot
{
public:
    UISlot(UIElement& parent, UIElement& child);
    virtual ~UISlot() = default;

    [[nodiscard]] UIElement& getParent() const { return *_parent; }
    [[nodiscard]] UIElement& getChild() const { return *_child; }
    virtual void appendRuntimeDiagnostics(nlohmann::json& node) const;

    /// Typed access remains open to user-defined UISlot subclasses; adding a
    /// slot type does not require editing an engine-owned enum.
    template <typename T>
    [[nodiscard]] T* as() { return dynamic_cast<T*>(this); }
    template <typename T>
    [[nodiscard]] const T* as() const { return dynamic_cast<const T*>(this); }

protected:
    void invalidateMeasure() const;
    void invalidateArrange() const;

private:
    UIElement* _parent = nullptr;
    UIElement* _child  = nullptr;
};

/// Four-side inset used by box/overlay slots and single-child padding.
/// `glm::vec2` overloads mean uniform horizontal / vertical (left=right, top=bottom).
/// Not an aggregate: `{x, y}` must not silently become left/top with zero right/bottom.
struct FMargin
{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;

    FMargin() = default;
    constexpr FMargin(float left_, float top_, float right_, float bottom_)
        : left(left_)
        , top(top_)
        , right(right_)
        , bottom(bottom_)
    {
    }

    [[nodiscard]] static FMargin all(float value) { return {value, value, value, value}; }
    [[nodiscard]] static FMargin hv(float horizontal, float vertical)
    {
        return {horizontal, vertical, horizontal, vertical};
    }
    [[nodiscard]] static FMargin hv(glm::vec2 value) { return hv(value.x, value.y); }

    [[nodiscard]] float horizontal() const { return left + right; }
    [[nodiscard]] float vertical() const { return top + bottom; }
    [[nodiscard]] glm::vec2 size() const { return {horizontal(), vertical()}; }
    [[nodiscard]] glm::vec2 minOffset() const { return {left, top}; }

    friend bool operator==(const FMargin&, const FMargin&) = default;
};

enum class EUIBoxSlotSizeRule : uint8_t
{
    Auto,
    Fill,
};

enum class EUIBoxSlotCrossAlignment : uint8_t
{
    Stretch,
    Start,
    Center,
    End,
};

/// Layout data carried by one UIBoxLayout parent-child edge.
class YA_GUI_API UIBoxSlot final : public UISlot
{
public:
    UIBoxSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] EUIBoxSlotSizeRule getSizeRule() const { return _sizeRule; }
    [[nodiscard]] float getWeight() const { return _weight; }
    [[nodiscard]] const FMargin& getMargin() const { return _margin; }
    [[nodiscard]] EUIBoxSlotCrossAlignment getCrossAlignment() const { return _crossAlignment; }
    [[nodiscard]] const glm::vec2& getMinSize() const { return _minSize; }
    [[nodiscard]] const glm::vec2& getMaxSize() const { return _maxSize; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }
    [[nodiscard]] bool participatesInLayout() const { return _bParticipatesInLayout; }
    [[nodiscard]] bool reservesSpaceWhenHidden() const { return _bReserveSpaceWhenHidden; }

    void setSizeRule(EUIBoxSlotSizeRule value);
    void setWeight(float value);
    void setMargin(FMargin value);
    void setMargin(glm::vec2 value) { setMargin(FMargin::hv(value)); }
    void setCrossAlignment(EUIBoxSlotCrossAlignment value);
    void setMinSize(glm::vec2 value);
    void setMaxSize(glm::vec2 value);
    void setPreferredSize(glm::vec2 value);
    void setParticipatesInLayout(bool value);
    void setReserveSpaceWhenHidden(bool value);
    void apply(const struct FBoxSlotArgs& args);
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;

private:
    EUIBoxSlotSizeRule        _sizeRule = EUIBoxSlotSizeRule::Auto;
    float                     _weight   = 1.0f;
    FMargin                   _margin{};
    EUIBoxSlotCrossAlignment  _crossAlignment = EUIBoxSlotCrossAlignment::Stretch;
    glm::vec2                 _minSize = {0.0f, 0.0f};
    glm::vec2                 _maxSize = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    glm::vec2                 _preferredSize = {0.0f, 0.0f};
    bool                      _bParticipatesInLayout = true;
    bool                      _bReserveSpaceWhenHidden = true;
};

/// Construct-time box slot intent. Applied after the child is attached so the
/// parent-owned UIBoxSlot already exists.
struct FBoxSlotArgs
{
    EUIBoxSlotSizeRule       sizeRule        = EUIBoxSlotSizeRule::Auto;
    float                    weight          = 1.0f;
    FMargin                  margin          = {};
    EUIBoxSlotCrossAlignment crossAlignment  = EUIBoxSlotCrossAlignment::Stretch;
    /// Non-zero on an axis overrides the child's desired size for that axis.
    glm::vec2                preferredSize  = {0.0f, 0.0f};
};

/// Parent-owned layout algorithm. Layout owns measure/arrange only; visual
/// ownership remains UIElement/WidgetTree and child intent lives in UISlot.
class YA_GUI_API UILayout
{
public:
    virtual ~UILayout() = default;

    void setOwner(UIElement& owner) { _owner = &owner; }
    [[nodiscard]] virtual std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const;
    [[nodiscard]] virtual glm::vec2 measure(const UIElement& parent) const = 0;
    /// Counted public entry. Hosts that override `layoutAssigned` currently
    /// call this without `tryReuseAssignedLayout()`; GAH-002 records that
    /// bypass, GAH-201 must keep the counter when skip is unified.
    void arrange(UIElement& parent, const Rect2D& rect) const;
    [[nodiscard]] uint32_t getArrangeCount() const { return _arrangeCount; }
    void resetArrangeCount() const { _arrangeCount = 0; }

protected:
    virtual void onArrange(UIElement& parent, const Rect2D& rect) const = 0;
    void invalidateMeasure() const;
    void invalidateArrange() const;
    /// Invalidate the owner's whole subtree paint context (clip/visibility),
    /// without re-running measure/arrange.
    void invalidateSubtreePaint() const;

    /// The single entry point for path-A child rect assignment.
    ///
    /// Every onArrange() must route child rects through here instead of calling
    /// child.layoutAssigned() directly: this is what makes the "path-A parents
    /// ignore child anchors" contract observable, so an author who wrote
    /// setAnchors()/fillWidth() on a child of a box/scroll/split/overlay gets a
    /// diagnostic instead of a silently dropped intent.
    void assignChildRect(UIElement& child, const Rect2D& rect) const;

private:
    UIElement* _owner = nullptr;
    mutable uint32_t _arrangeCount = 0;
};

/// The first formal layout: horizontal/vertical box packing with layout-owned
/// container properties and per-child UIBoxSlot data.
class YA_GUI_API UIBoxLayout final : public UILayout
{
public:
    [[nodiscard]] EWidgetBoxLayout getDirection() const { return _direction; }
    [[nodiscard]] float getSpacing() const { return _spacing; }
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    [[nodiscard]] EWidgetMainAxisAlignment getMainAxisAlignment() const { return _mainAxisAlignment; }
    [[nodiscard]] bool clipsChildren() const { return _bClipChildren; }
    [[nodiscard]] bool stretchesLastChild() const { return _bStretchLastChild; }

    void setDirection(EWidgetBoxLayout value);
    void setSpacing(float value);
    void setPadding(glm::vec2 value);
    void setMainAxisAlignment(EWidgetMainAxisAlignment value);
    void setClipsChildren(bool value);
    void setStretchLastChild(bool value);

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    EWidgetBoxLayout         _direction         = EWidgetBoxLayout::Horizontal;
    float                    _spacing           = 4.0f;
    glm::vec2                _padding           = {0.0f, 0.0f};
    EWidgetMainAxisAlignment _mainAxisAlignment = EWidgetMainAxisAlignment::Start;
    bool                     _bClipChildren     = false;
    bool                     _bStretchLastChild = false;
};

/// Layout for a single content child that fills an inset content rect.
/// Buttons and SizeBox reuse this instead of each reimplementing
/// "parent rect minus padding".
/// How a child is placed on one axis inside a box the parent already owns.
/// Fill stretches that axis; otherwise the child keeps its desired size and
/// Start/Center/End place it. Shared by UIOverlaySlot (multiple stacked
/// children) and UISingleChildLayout (scroll / size box / split / button ...),
/// which answer the same question per axis.
enum class EUIOverlayAlignment : uint8_t
{
    Fill,
    Start,
    Center,
    End,
};

class YA_GUI_API UISingleChildLayout final : public UILayout
{
public:
    [[nodiscard]] const FMargin& getPadding() const { return _padding; }
    void setPadding(FMargin value);
    void setPadding(glm::vec2 value) { setPadding(FMargin::hv(value)); }

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    FMargin _padding{};
};

/// Overlay slot: one child independently aligned inside the parent rect
/// (UMG Overlay / stacked Control). Fill stretches that axis; otherwise the
/// child keeps its desired size and Start/Center/End place it.
class YA_GUI_API UIOverlaySlot final : public UISlot
{
public:
    UIOverlaySlot(UIElement& parent, UIElement& child);

    [[nodiscard]] EUIOverlayAlignment getHAlign() const { return _hAlign; }
    [[nodiscard]] EUIOverlayAlignment getVAlign() const { return _vAlign; }
    [[nodiscard]] const FMargin& getPadding() const { return _padding; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }

    void setHAlign(EUIOverlayAlignment value);
    void setVAlign(EUIOverlayAlignment value);
    void setPadding(FMargin value);
    void setPreferredSize(glm::vec2 value);
    void setPadding(glm::vec2 value) { setPadding(FMargin::hv(value)); }
    void apply(const struct FOverlaySlotArgs& args);
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;

private:
    EUIOverlayAlignment _hAlign  = EUIOverlayAlignment::Fill;
    EUIOverlayAlignment _vAlign  = EUIOverlayAlignment::Fill;
    FMargin             _padding{};
    glm::vec2           _preferredSize = {0.0f, 0.0f};
};

struct FOverlaySlotArgs
{
    EUIOverlayAlignment hAlign  = EUIOverlayAlignment::Fill;
    EUIOverlayAlignment vAlign  = EUIOverlayAlignment::Fill;
    FMargin             padding = {};
    /// Non-zero on an axis overrides the child's desired size for that axis.
    glm::vec2           preferredSize = {0.0f, 0.0f};
};

/// Stacked children sharing one parent rect. Each child is arranged through
/// its UIOverlaySlot; child canvas anchors are ignored (layoutAssigned).
class YA_GUI_API UIOverlayLayout final : public UILayout
{
public:
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;
};

enum class ESplitOrientation : uint8_t
{
    Vertical,   // divider runs vertically: panes sit side by side (left/right)
    Horizontal, // divider runs horizontally: panes stack (top/bottom)
};

/// Geometry policy for a two-pane split. Drag state stays on UISplitPane;
/// orientation, ratio, limits, padding and child arrangement live here.
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
    /// carried by a single-child slot (fill by default).
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    void clampRatio() const;
    [[nodiscard]] float axisExtent(const Rect2D& rect) const;
    [[nodiscard]] float axisPosition(const glm::vec2& point) const;

    ESplitOrientation _orientation = ESplitOrientation::Vertical;
    mutable float      _splitRatio = 0.5f;
    float              _minFirstExtent = 40.0f;
    float              _minSecondExtent = 40.0f;
    float              _dividerThickness = 6.0f;
    glm::vec2          _padding = {0.0f, 0.0f};
    mutable Rect2D     _contentRect{};
};

struct FCanvasSlotArgs;

/// Canvas slot: the anchor-owning parent-child edge. The child's rect is
/// resolved from `anchorMin/anchorMax` against the parent rect, plus an offset
/// and optional min/max clamps. This replaces the historical
/// Child placement is authored on the parent-owned slot edge.
/// pattern: anchor intent lives on the parent->child edge, so a non-canvas
/// parent (which never reads this slot) cannot silently drop it.
///
/// Canvas is a LAYOUT, not a widget: any host that installs UICanvasLayout
/// (UIPanel today) can carry these edges.
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
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;

private:
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

/// Construct-time canvas slot intent. Defaults to a visible top-left child
/// sized from desired/intrinsic content (Auto/Auto). Use fill or explicit size
/// to express stronger placement intent.
struct FCanvasSlotArgs
{
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

/// Canvas layout: children are positioned by anchor rects against the parent
/// content rect. This is the layout form of the historical "path-B" panel
/// behaviour; it is NOT bound to UIPanel - any host may install it.
class YA_GUI_API UICanvasLayout final : public UILayout
{
public:
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    void setPadding(glm::vec2 value);

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

    /// Resolve one child rect from its canvas slot against the parent content
    /// rect. Delegates to UIElement::resolveCanvasRect() with slot-authored
    /// size so the canvas layout never reads child `_size`.
    [[nodiscard]] static Rect2D resolveChildRect(const UIElement&    child,
                                                 const UICanvasSlot& slot,
                                                 const Rect2D&       contentRect);

private:
    glm::vec2 _padding = {0.0f, 0.0f};
};

enum class EScrollAxis : uint8_t
{
    Vertical,
    Horizontal,
};

/// Layout data carried by one UITableLayout parent-child edge: the cell
/// position of the child inside the table grid.
class YA_GUI_API UITableSlot final : public UISlot
{
public:
    UITableSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] int getRow() const { return _row; }
    [[nodiscard]] int getColumn() const { return _column; }

    void setCell(int row, int column);

private:
    int _row    = 0;
    int _column = 0;
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

/// Geometry and state policy for one scrollable content child. The viewport
/// widget owns clipping/input; this layout owns desired content extent,
/// offset clamping and assigned content rect.
class YA_GUI_API UIScrollLayout final : public UILayout
{
public:
    [[nodiscard]] EScrollAxis getAxis() const { return _axis; }
    [[nodiscard]] float getScrollOffset() const { return _scrollOffset; }
    [[nodiscard]] float getScrollStep() const { return _scrollStep; }
    [[nodiscard]] float getMaxScrollOffset() const { return _maxScrollOffset; }
    [[nodiscard]] bool isScrollable() const { return _maxScrollOffset > 0.0f; }

    void setAxis(EScrollAxis value);
    void setScrollOffset(float value);
    void setScrollStep(float value);
    /// Applies the pointer wheel delta along the configured axis. Returns
    /// true only if the offset changed; callers then consume the route.
    bool scroll(const glm::vec2& wheelDelta);

    /// The viewport owns the scrolling axis extent itself; the child edge only
    /// carries cross-axis placement (fill by default, or align at desired
    /// size), so scroll content uses the shared single-child slot contract.
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;

    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    EScrollAxis   _axis = EScrollAxis::Vertical;
    mutable float _scrollOffset = 0.0f;
    float         _scrollStep = 40.0f;
    mutable float _maxScrollOffset = 0.0f;
};

} // namespace ya
