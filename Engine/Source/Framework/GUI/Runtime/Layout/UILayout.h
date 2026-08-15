#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"
#include "GUI/Widgets/UIElement.h"

#include <glm/glm.hpp>

#include <limits>
#include <memory>

namespace ya
{

struct WidgetTree;

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

protected:
    void invalidateMeasure() const;
    void invalidateArrange() const;

private:
    UIElement* _parent = nullptr;
    UIElement* _child  = nullptr;
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
    [[nodiscard]] const glm::vec2& getMargin() const { return _margin; }
    [[nodiscard]] EUIBoxSlotCrossAlignment getCrossAlignment() const { return _crossAlignment; }
    [[nodiscard]] const glm::vec2& getMinSize() const { return _minSize; }
    [[nodiscard]] const glm::vec2& getMaxSize() const { return _maxSize; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }
    [[nodiscard]] bool participatesInLayout() const { return _bParticipatesInLayout; }
    [[nodiscard]] bool reservesSpaceWhenHidden() const { return _bReserveSpaceWhenHidden; }

    void setSizeRule(EUIBoxSlotSizeRule value);
    void setWeight(float value);
    void setMargin(glm::vec2 value);
    void setCrossAlignment(EUIBoxSlotCrossAlignment value);
    void setMinSize(glm::vec2 value);
    void setMaxSize(glm::vec2 value);
    void setPreferredSize(glm::vec2 value);
    void setParticipatesInLayout(bool value);
    void setReserveSpaceWhenHidden(bool value);

private:
    EUIBoxSlotSizeRule        _sizeRule = EUIBoxSlotSizeRule::Auto;
    float                     _weight   = 1.0f;
    glm::vec2                 _margin  = {0.0f, 0.0f};
    EUIBoxSlotCrossAlignment  _crossAlignment = EUIBoxSlotCrossAlignment::Stretch;
    glm::vec2                 _minSize = {0.0f, 0.0f};
    glm::vec2                 _maxSize = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    glm::vec2                 _preferredSize = {0.0f, 0.0f};
    bool                      _bParticipatesInLayout = true;
    bool                      _bReserveSpaceWhenHidden = true;
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
    virtual void arrange(UIElement& parent, const Rect2D& rect) const = 0;

protected:
    void invalidateMeasure() const;
    void invalidateArrange() const;
    /// Invalidate the owner's whole subtree paint context (clip/visibility),
    /// without re-running measure/arrange.
    void invalidateSubtreePaint() const;

private:
    UIElement* _owner = nullptr;
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
    void arrange(UIElement& parent, const Rect2D& rect) const override;

private:
    EWidgetBoxLayout         _direction         = EWidgetBoxLayout::Horizontal;
    float                    _spacing           = 4.0f;
    glm::vec2                _padding           = {0.0f, 0.0f};
    EWidgetMainAxisAlignment _mainAxisAlignment = EWidgetMainAxisAlignment::Start;
    bool                     _bClipChildren     = false;
    bool                     _bStretchLastChild = false;
};

/// Layout for a single content child that fills an inset content rect.
/// Buttons are the first consumer; popup/content controls can reuse it
/// instead of each reimplementing "parent rect minus padding".
class YA_GUI_API UISingleChildLayout final : public UILayout
{
public:
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    void setPadding(glm::vec2 value);

    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void arrange(UIElement& parent, const Rect2D& rect) const override;

private:
    glm::vec2 _padding = {0.0f, 0.0f};
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

    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void arrange(UIElement& parent, const Rect2D& rect) const override;

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

enum class EScrollAxis : uint8_t
{
    Vertical,
    Horizontal,
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

    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void arrange(UIElement& parent, const Rect2D& rect) const override;

private:
    EScrollAxis   _axis = EScrollAxis::Vertical;
    mutable float _scrollOffset = 0.0f;
    float         _scrollStep = 40.0f;
    mutable float _maxScrollOffset = 0.0f;
};

} // namespace ya
