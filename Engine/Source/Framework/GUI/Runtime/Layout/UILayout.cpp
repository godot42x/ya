#include "GUI/Layout/UILayout.h"

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

const UIBoxSlot* getBoxSlot(const UIElement& parent, const UIElement& child)
{
    return dynamic_cast<const UIBoxSlot*>(parent.getSlotForChild(child));
}

glm::vec2 resolveDesiredSize(const UIElement& parent, const UIElement& child)
{
    glm::vec2 desired = child.computeDesiredSize();
    if (const UIBoxSlot* slot = getBoxSlot(parent, child)) {
        const glm::vec2 preferred = slot->getPreferredSize();
        if (preferred.x > 0.0f) {
            desired.x = preferred.x;
        }
        if (preferred.y > 0.0f) {
            desired.y = preferred.y;
        }
        desired = glm::clamp(desired, slot->getMinSize(), slot->getMaxSize());
    }
    return glm::max(desired, glm::vec2(0.0f));
}

bool participatesInBox(const UIElement& parent, const UIElement& child)
{
    if (!child.participatesInLayout()) {
        return false;
    }
    const UIBoxSlot* slot = getBoxSlot(parent, child);
    return !slot ||
           (slot->participatesInLayout() &&
            (child.getVisibility() != EWidgetVisibility::Hidden || slot->reservesSpaceWhenHidden()));
}

glm::vec2 slotMargin(const UIElement& parent, const UIElement& child)
{
    if (const UIBoxSlot* slot = getBoxSlot(parent, child)) {
        return glm::max(slot->getMargin(), glm::vec2(0.0f));
    }
    return {};
}

} // namespace

UISlot::UISlot(UIElement& parent, UIElement& child)
    : _parent(&parent)
    , _child(&child)
{
}

void UISlot::invalidateMeasure() const
{
    if (WidgetTree* tree = _parent->getTree()) {
        tree->invalidateLayout();
    }
}

void UISlot::invalidateArrange() const
{
    invalidateMeasure();
}

UIBoxSlot::UIBoxSlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UIBoxSlot::setSizeRule(EUIBoxSlotSizeRule value)
{
    if (_sizeRule != value) {
        _sizeRule = value;
        invalidateArrange();
    }
}

void UIBoxSlot::setWeight(float value)
{
    const float clamped = std::max(value, 0.0f);
    if (_weight != clamped) {
        _weight = clamped;
        invalidateArrange();
    }
}

void UIBoxSlot::setMargin(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_margin != value) {
        _margin = value;
        invalidateMeasure();
    }
}

void UIBoxSlot::setCrossAlignment(EUIBoxSlotCrossAlignment value)
{
    if (_crossAlignment != value) {
        _crossAlignment = value;
        invalidateArrange();
    }
}

void UIBoxSlot::setMinSize(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_minSize != value) {
        _minSize = value;
        _maxSize = glm::max(_maxSize, _minSize);
        invalidateMeasure();
    }
}

void UIBoxSlot::setMaxSize(glm::vec2 value)
{
    value = glm::max(value, _minSize);
    if (_maxSize != value) {
        _maxSize = value;
        invalidateMeasure();
    }
}

void UIBoxSlot::setPreferredSize(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_preferredSize != value) {
        _preferredSize = value;
        invalidateMeasure();
    }
}

void UIBoxSlot::setParticipatesInLayout(bool value)
{
    if (_bParticipatesInLayout != value) {
        _bParticipatesInLayout = value;
        invalidateMeasure();
    }
}

void UIBoxSlot::setReserveSpaceWhenHidden(bool value)
{
    if (_bReserveSpaceWhenHidden != value) {
        _bReserveSpaceWhenHidden = value;
        invalidateMeasure();
    }
}

std::unique_ptr<UISlot> UILayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UISlot>(parent, child);
}

void UILayout::invalidateMeasure() const
{
    if (_owner) {
        if (WidgetTree* tree = _owner->getTree()) {
            tree->invalidateLayout();
        }
    }
}

void UILayout::invalidateArrange() const
{
    invalidateMeasure();
}

void UILayout::invalidateSubtreePaint() const
{
    if (_owner) {
        _owner->invalidateSubtree();
    }
}

void UIBoxLayout::setDirection(EWidgetBoxLayout value)
{
    if (_direction != value) {
        _direction = value;
        invalidateMeasure();
    }
}

void UIBoxLayout::setSpacing(float value)
{
    const float clamped = std::max(value, 0.0f);
    if (_spacing != clamped) {
        _spacing = clamped;
        invalidateMeasure();
    }
}

void UIBoxLayout::setPadding(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_padding != value) {
        _padding = value;
        invalidateMeasure();
    }
}

void UIBoxLayout::setMainAxisAlignment(EWidgetMainAxisAlignment value)
{
    if (_mainAxisAlignment != value) {
        _mainAxisAlignment = value;
        invalidateArrange();
    }
}

void UIBoxLayout::setClipsChildren(bool value)
{
    if (_bClipChildren != value) {
        _bClipChildren = value;
        // Clip is an inherited paint context, not geometry: repaint the whole
        // subtree without re-running measure/arrange (SubtreePaintContext).
        invalidateSubtreePaint();
    }
}

void UIBoxLayout::setStretchLastChild(bool value)
{
    if (_bStretchLastChild != value) {
        _bStretchLastChild = value;
        invalidateArrange();
    }
}

std::unique_ptr<UISlot> UIBoxLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIBoxSlot>(parent, child);
}

glm::vec2 UIBoxLayout::measure(const UIElement& parent) const
{
    const bool bHorizontal = _direction == EWidgetBoxLayout::Horizontal;
    float      main        = 0.0f;
    float      cross       = 0.0f;
    size_t     count       = 0;
    for (const auto& childRef : parent.getChildren()) {
        const UIElement& child = *childRef;
        if (!participatesInBox(parent, child)) {
            continue;
        }
        const glm::vec2 desired = resolveDesiredSize(parent, child);
        const glm::vec2 margin  = slotMargin(parent, child);
        main += (bHorizontal ? desired.x : desired.y) + (bHorizontal ? margin.x : margin.y) * 2.0f;
        cross = std::max(cross, (bHorizontal ? desired.y : desired.x) + (bHorizontal ? margin.y : margin.x) * 2.0f);
        ++count;
    }
    if (count > 1) {
        main += static_cast<float>(count - 1) * _spacing;
    }
    return bHorizontal
               ? glm::vec2(main + _padding.x * 2.0f, cross + _padding.y * 2.0f)
               : glm::vec2(cross + _padding.x * 2.0f, main + _padding.y * 2.0f);
}

void UIBoxLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    Rect2D content = rect;
    content.pos += _padding;
    content.extent = glm::max(content.extent - _padding * 2.0f, glm::vec2(0.0f));

    const bool  bHorizontal = _direction == EWidgetBoxLayout::Horizontal;
    const float contentMain = bHorizontal ? content.extent.x : content.extent.y;
    const float contentCross = bHorizontal ? content.extent.y : content.extent.x;

    struct FEntry
    {
        UIElement*       child = nullptr;
        const UIBoxSlot* slot  = nullptr;
        glm::vec2        desired{};
        glm::vec2        margin{};
        float            mainExtent = 0.0f;
        float            maxMain = std::numeric_limits<float>::max();
        float            weight = 0.0f;
        bool             bFill = false;
    };
    std::vector<FEntry> entries;
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!participatesInBox(parent, *child)) {
            continue;
        }
        const UIBoxSlot* slot = getBoxSlot(parent, *child);
        const float minMain = slot ? (bHorizontal ? slot->getMinSize().x : slot->getMinSize().y) : 0.0f;
        const float maxMain = slot ? (bHorizontal ? slot->getMaxSize().x : slot->getMaxSize().y)
                                   : std::numeric_limits<float>::max();
        entries.push_back(FEntry{
            .child       = child,
            .slot        = slot,
            .desired     = resolveDesiredSize(parent, *child),
            .margin      = slotMargin(parent, *child),
            .mainExtent  = minMain,
            .maxMain     = maxMain,
            .weight      = slot ? std::max(slot->getWeight(), 0.0f) : 1.0f,
            .bFill       = slot && slot->getSizeRule() == EUIBoxSlotSizeRule::Fill,
        });
    }
    if (_bStretchLastChild && !entries.empty()) {
        entries.back().bFill = true;
    }

    float packedMain = entries.empty() ? 0.0f : static_cast<float>(entries.size() - 1) * _spacing;
    for (FEntry& entry : entries) {
        const float desiredMain = bHorizontal ? entry.desired.x : entry.desired.y;
        const float marginMain  = bHorizontal ? entry.margin.x : entry.margin.y;
        entry.mainExtent = entry.bFill ? entry.mainExtent : desiredMain;
        packedMain += entry.mainExtent + marginMain * 2.0f;
    }

    float remainder = std::max(0.0f, contentMain - packedMain);
    while (remainder > 0.0f) {
        float eligibleWeight = 0.0f;
        for (FEntry& entry : entries) {
            if (entry.bFill && entry.mainExtent < entry.maxMain && entry.weight > 0.0f) {
                eligibleWeight += entry.weight;
            }
        }
        if (eligibleWeight == 0.0f) {
            break;
        }

        float allocated = 0.0f;
        for (FEntry& entry : entries) {
            if (!entry.bFill || entry.mainExtent >= entry.maxMain || entry.weight <= 0.0f) {
                continue;
            }
            const float addition = std::min(remainder * entry.weight / eligibleWeight,
                                            entry.maxMain - entry.mainExtent);
            entry.mainExtent += addition;
            allocated += addition;
        }
        if (allocated <= 0.0f) {
            break;
        }
        packedMain += allocated;
        remainder -= allocated;
    }

    float cursor = bHorizontal ? content.pos.x : content.pos.y;
    switch (_mainAxisAlignment) {
    case EWidgetMainAxisAlignment::Center:
        cursor += std::max(0.0f, (contentMain - packedMain) * 0.5f);
        break;
    case EWidgetMainAxisAlignment::End:
        cursor += std::max(0.0f, contentMain - packedMain);
        break;
    case EWidgetMainAxisAlignment::Start:
        break;
    }

    for (FEntry& entry : entries) {
        const float marginMain  = bHorizontal ? entry.margin.x : entry.margin.y;
        const float marginCross = bHorizontal ? entry.margin.y : entry.margin.x;
        const float desiredCross = bHorizontal ? entry.desired.y : entry.desired.x;
        const float availableCross = std::max(0.0f, contentCross - marginCross * 2.0f);
        float crossExtent = availableCross;
        float crossPos = (bHorizontal ? content.pos.y : content.pos.x) + marginCross;

        const EUIBoxSlotCrossAlignment crossAlignment =
            entry.slot ? entry.slot->getCrossAlignment() : EUIBoxSlotCrossAlignment::Stretch;
        if (crossAlignment != EUIBoxSlotCrossAlignment::Stretch) {
            crossExtent = std::min(availableCross, desiredCross);
            const float slack = std::max(0.0f, availableCross - crossExtent);
            if (crossAlignment == EUIBoxSlotCrossAlignment::Center) {
                crossPos += slack * 0.5f;
            }
            else if (crossAlignment == EUIBoxSlotCrossAlignment::End) {
                crossPos += slack;
            }
        }

        cursor += marginMain;
        Rect2D childRect;
        if (bHorizontal) {
            childRect = {
                .pos    = {cursor, crossPos},
                .extent = {entry.mainExtent, crossExtent},
            };
        }
        else {
            childRect = {
                .pos    = {crossPos, cursor},
                .extent = {crossExtent, entry.mainExtent},
            };
        }
        entry.child->layoutAssigned(childRect);
        cursor += entry.mainExtent + marginMain + _spacing;
    }
}

void UISingleChildLayout::setPadding(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_padding != value) {
        _padding = value;
        invalidateMeasure();
    }
}

glm::vec2 UISingleChildLayout::measure(const UIElement& parent) const
{
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            return glm::max(child->computeDesiredSize() + _padding * 2.0f, glm::vec2(0.0f));
        }
    }
    return _padding * 2.0f;
}

void UISingleChildLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    Rect2D contentRect = rect;
    contentRect.pos += _padding;
    contentRect.extent = glm::max(contentRect.extent - _padding * 2.0f, glm::vec2(0.0f));
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(contentRect);
            return;
        }
    }
}

float UISplitLayout::axisExtent(const Rect2D& rect) const
{
    return _orientation == ESplitOrientation::Vertical ? rect.extent.x : rect.extent.y;
}

float UISplitLayout::axisPosition(const glm::vec2& point) const
{
    return _orientation == ESplitOrientation::Vertical ? point.x : point.y;
}

float UISplitLayout::axisCoordinate(const glm::vec2& point) const
{
    return axisPosition(point);
}

void UISplitLayout::clampRatio() const
{
    const float contentExtent = axisExtent(_contentRect);
    if (contentExtent <= 0.0f) {
        _splitRatio = 0.5f;
        return;
    }
    const float minRatio = std::clamp(_minFirstExtent / contentExtent, 0.0f, 1.0f);
    const float maxRatio = std::clamp(1.0f - _minSecondExtent / contentExtent, 0.0f, 1.0f);
    _splitRatio = std::clamp(_splitRatio, std::min(minRatio, maxRatio), std::max(minRatio, maxRatio));
}

void UISplitLayout::setOrientation(ESplitOrientation value)
{
    if (_orientation != value) {
        _orientation = value;
        invalidateMeasure();
    }
}

void UISplitLayout::setSplitRatio(float value)
{
    const float previous = _splitRatio;
    _splitRatio = std::clamp(value, 0.0f, 1.0f);
    if (axisExtent(_contentRect) > 0.0f) {
        clampRatio();
    }
    if (_splitRatio != previous) {
        invalidateArrange();
    }
}

void UISplitLayout::setMinFirstExtent(float value)
{
    value = std::max(value, 0.0f);
    if (_minFirstExtent != value) {
        _minFirstExtent = value;
        invalidateArrange();
    }
}

void UISplitLayout::setMinSecondExtent(float value)
{
    value = std::max(value, 0.0f);
    if (_minSecondExtent != value) {
        _minSecondExtent = value;
        invalidateArrange();
    }
}

void UISplitLayout::setDividerThickness(float value)
{
    value = std::max(value, 0.0f);
    if (_dividerThickness != value) {
        _dividerThickness = value;
        invalidateMeasure();
    }
}

void UISplitLayout::setPadding(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_padding != value) {
        _padding = value;
        invalidateMeasure();
    }
}

Rect2D UISplitLayout::getDividerRect() const
{
    const float thickness = _dividerThickness;
    const float dividerCenter = axisPosition(_contentRect.pos) + axisExtent(_contentRect) * _splitRatio;
    Rect2D divider = _contentRect;
    if (_orientation == ESplitOrientation::Vertical) {
        divider.pos.x = dividerCenter - thickness * 0.5f;
        divider.extent.x = thickness;
    }
    else {
        divider.pos.y = dividerCenter - thickness * 0.5f;
        divider.extent.y = thickness;
    }
    return divider;
}

glm::vec2 UISplitLayout::measure(const UIElement& parent) const
{
    const auto children = parent.getChildrenInPaintOrder();
    if (children.empty()) {
        return parent.getSize();
    }

    glm::vec2 desired{};
    size_t arrangedChildCount = 0;
    for (UIElement* child : children) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 childDesired = child->computeDesiredSize();
        if (_orientation == ESplitOrientation::Vertical) {
            desired.x += childDesired.x;
            desired.y = std::max(desired.y, childDesired.y);
        }
        else {
            desired.y += childDesired.y;
            desired.x = std::max(desired.x, childDesired.x);
        }
        if (++arrangedChildCount == 2) {
            break;
        }
    }
    if (_orientation == ESplitOrientation::Vertical) {
        desired.x += _dividerThickness;
    }
    else {
        desired.y += _dividerThickness;
    }
    return desired + _padding * 2.0f;
}

void UISplitLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    _contentRect = rect;
    _contentRect.pos += _padding;
    _contentRect.extent = glm::max(_contentRect.extent - _padding * 2.0f, glm::vec2(0.0f));
    const auto children = parent.getChildrenInPaintOrder();
    if (children.empty()) {
        return;
    }

    clampRatio();
    const float dividerCenter = axisPosition(_contentRect.pos) + axisExtent(_contentRect) * _splitRatio;
    Rect2D firstRect = _contentRect;
    Rect2D secondRect = _contentRect;
    if (_orientation == ESplitOrientation::Vertical) {
        firstRect.extent.x = std::max(0.0f, dividerCenter - _dividerThickness * 0.5f - firstRect.pos.x);
        secondRect.pos.x = dividerCenter + _dividerThickness * 0.5f;
        secondRect.extent.x =
            std::max(0.0f, _contentRect.pos.x + _contentRect.extent.x - secondRect.pos.x);
    }
    else {
        firstRect.extent.y = std::max(0.0f, dividerCenter - _dividerThickness * 0.5f - firstRect.pos.y);
        secondRect.pos.y = dividerCenter + _dividerThickness * 0.5f;
        secondRect.extent.y =
            std::max(0.0f, _contentRect.pos.y + _contentRect.extent.y - secondRect.pos.y);
    }

    children[0]->layoutAssigned(firstRect);
    if (children.size() >= 2) {
        children[1]->layoutAssigned(secondRect);
    }
}

void UIScrollLayout::setAxis(EScrollAxis value)
{
    if (_axis != value) {
        _axis = value;
        invalidateArrange();
    }
}

void UIScrollLayout::setScrollOffset(float value)
{
    value = std::max(value, 0.0f);
    if (_scrollOffset != value) {
        _scrollOffset = value;
        invalidateArrange();
    }
}

void UIScrollLayout::setScrollStep(float value)
{
    value = std::max(value, 0.0f);
    if (_scrollStep != value) {
        _scrollStep = value;
        invalidateArrange();
    }
}

bool UIScrollLayout::scroll(const glm::vec2& wheelDelta)
{
    const float delta = _axis == EScrollAxis::Vertical ? -wheelDelta.y : -wheelDelta.x;
    if (delta == 0.0f || !isScrollable()) {
        return false;
    }
    const float newOffset = std::clamp(_scrollOffset + delta * _scrollStep, 0.0f, _maxScrollOffset);
    if (newOffset == _scrollOffset) {
        return false;
    }
    _scrollOffset = newOffset;
    invalidateArrange();
    return true;
}

glm::vec2 UIScrollLayout::measure(const UIElement& parent) const
{
    return parent.getSize();
}

void UIScrollLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    const auto children = parent.getChildrenInPaintOrder();
    if (children.empty()) {
        _maxScrollOffset = 0.0f;
        _scrollOffset = 0.0f;
        return;
    }

    const glm::vec2 desired = children[0]->computeDesiredSize();
    const bool bVertical = _axis == EScrollAxis::Vertical;
    const float contentMain = bVertical ? std::max(desired.y, rect.extent.y)
                                        : std::max(desired.x, rect.extent.x);
    const float viewportMain = bVertical ? rect.extent.y : rect.extent.x;
    _maxScrollOffset = std::max(0.0f, contentMain - viewportMain);
    _scrollOffset = std::clamp(_scrollOffset, 0.0f, _maxScrollOffset);

    Rect2D contentRect = rect;
    if (bVertical) {
        contentRect.pos.y -= _scrollOffset;
        contentRect.extent = {rect.extent.x, contentMain};
    }
    else {
        contentRect.pos.x -= _scrollOffset;
        contentRect.extent = {contentMain, rect.extent.y};
    }
    children[0]->layoutAssigned(contentRect);
}

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ESplitOrientation)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EScrollAxis)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()
