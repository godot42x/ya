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
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UIBoxSlot>() : nullptr;
}

const UIOverlaySlot* getOverlaySlot(const UIElement& parent, const UIElement& child)
{
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UIOverlaySlot>() : nullptr;
}

const UICanvasSlot* getCanvasSlot(const UIElement& parent, const UIElement& child)
{
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UICanvasSlot>() : nullptr;
}

/// Per-axis placement inside a box the parent already owns: Fill stretches,
/// anything else keeps the child's desired size and places it. Shared by the
/// overlay and single-child layouts, which answer the same question per axis.
float overlayAxis(float start, float available, float desired, EUIOverlayAlignment align)
{
    if (align == EUIOverlayAlignment::Fill) {
        return start;
    }
    const float extent = std::min(desired, available);
    if (align == EUIOverlayAlignment::Center) {
        return start + std::max(0.0f, (available - extent) * 0.5f);
    }
    if (align == EUIOverlayAlignment::End) {
        return start + std::max(0.0f, available - extent);
    }
    return start;
}

float overlayExtent(float available, float desired, EUIOverlayAlignment align)
{
    if (align == EUIOverlayAlignment::Fill) {
        return std::max(0.0f, available);
    }
    return std::max(0.0f, std::min(desired, available));
}

const UIOverlaySlot* getSingleChildSlot(const UIElement& parent, const UIElement& child)
{
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UIOverlaySlot>() : nullptr;
}

glm::vec2 resolveDesiredSize(const UIElement& parent, const UIElement& child)
{
    glm::vec2 desired = child.computeDesiredSize();
    auto overlayAuthored = [&](const glm::vec2& authored) {
        if (authored.x > 0.0f) {
            desired.x = authored.x;
        }
        if (authored.y > 0.0f) {
            desired.y = authored.y;
        }
    };
    if (const UIBoxSlot* slot = getBoxSlot(parent, child)) {
        overlayAuthored(slot->getPreferredSize());
        desired = glm::clamp(desired, slot->getMinSize(), slot->getMaxSize());
    }
    else if (const UIOverlaySlot* slot = getOverlaySlot(parent, child)) {
        overlayAuthored(slot->getPreferredSize());
    }
    else if (const UIOverlaySlot* slot = getSingleChildSlot(parent, child)) {
        overlayAuthored(slot->getPreferredSize());
    }
    else if (const UICanvasSlot* slot = getCanvasSlot(parent, child)) {
        const glm::vec2 fixed     = slot->getFixedSize();
        const glm::vec2 preferred = slot->getPreferredSize();
        overlayAuthored({fixed.x != 0.0f ? fixed.x : preferred.x,
                         fixed.y != 0.0f ? fixed.y : preferred.y});
        desired = glm::clamp(desired, slot->getMinSize(), slot->getMaxSize());
    }
    return glm::max(desired, glm::vec2(0.0f));
}

/// Re-place one axis of an already-computed child rect according to the child's
/// single-child slot. Used by parents that own the other axis themselves
/// (split owns the main axis via the ratio, scroll owns it via the content
/// extent): `bCrossIsY` selects which component is the cross axis.
Rect2D applyCrossAlign(const UIElement& parent, const UIElement& child, const Rect2D& rect, bool bCrossIsY)
{
    EUIOverlayAlignment crossAlign = EUIOverlayAlignment::Fill;
    if (const UIOverlaySlot* slot = getSingleChildSlot(parent, child)) {
        crossAlign = bCrossIsY ? slot->getVAlign() : slot->getHAlign();
    }
    if (crossAlign == EUIOverlayAlignment::Fill) {
        return rect;
    }
    const glm::vec2 desired = resolveDesiredSize(parent, child);
    Rect2D          result  = rect;
    if (bCrossIsY) {
        result.pos.y    = overlayAxis(rect.pos.y, rect.extent.y, desired.y, crossAlign);
        result.extent.y = overlayExtent(rect.extent.y, desired.y, crossAlign);
    }
    else {
        result.pos.x    = overlayAxis(rect.pos.x, rect.extent.x, desired.x, crossAlign);
        result.extent.x = overlayExtent(rect.extent.x, desired.x, crossAlign);
    }
    return result;
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

FMargin slotMargin(const UIElement& parent, const UIElement& child)
{
    if (const UIBoxSlot* slot = getBoxSlot(parent, child)) {
        const FMargin m = slot->getMargin();
        return {
            std::max(m.left, 0.0f),
            std::max(m.top, 0.0f),
            std::max(m.right, 0.0f),
            std::max(m.bottom, 0.0f),
        };
    }
    return {};
}

} // namespace

UISlot::UISlot(UIElement& parent, UIElement& child)
    : _parent(&parent)
    , _child(&child)
{
}

void UISlot::appendRuntimeDiagnostics(nlohmann::json& node) const
{
    node["type"] = "base";
}

void UISlot::invalidateMeasure() const
{
    if (WidgetTree* tree = _parent->getTree()) {
        tree->invalidateLayout();
    }
}

UICanvasSlot::UICanvasSlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UICanvasSlot::setAnchorMin(glm::vec2 value)
{
    if (_anchorMin == value) {
        return;
    }
    _anchorMin = value;
    invalidateArrange();
}

void UICanvasSlot::setAnchorMax(glm::vec2 value)
{
    if (_anchorMax == value) {
        return;
    }
    _anchorMax = value;
    invalidateArrange();
}

void UICanvasSlot::setOffset(glm::vec2 value)
{
    if (_offset == value) {
        return;
    }
    _offset = value;
    invalidateArrange();
}

void UICanvasSlot::setMinSize(glm::vec2 value)
{
    if (_minSize == value) {
        return;
    }
    _minSize = value;
    invalidateArrange();
}

void UICanvasSlot::setMaxSize(glm::vec2 value)
{
    if (_maxSize == value) {
        return;
    }
    _maxSize = value;
    invalidateArrange();
}

void UICanvasSlot::setOffsets(FMargin value)
{
    if (_offsets == value) {
        return;
    }
    _offsets = value;
    invalidateArrange();
}

void UICanvasSlot::setAlignmentH(EWidgetAlignH value)
{
    if (_alignmentH == value) {
        return;
    }
    _alignmentH = value;
    invalidateArrange();
}

void UICanvasSlot::setAlignmentV(EWidgetAlignV value)
{
    if (_alignmentV == value) {
        return;
    }
    _alignmentV = value;
    invalidateArrange();
}

void UICanvasSlot::setWidthSizeMode(EWidgetSizeMode value)
{
    if (_widthSizeMode == value) {
        return;
    }
    _widthSizeMode = value;
    invalidateMeasure();
}

void UICanvasSlot::setHeightSizeMode(EWidgetSizeMode value)
{
    if (_heightSizeMode == value) {
        return;
    }
    _heightSizeMode = value;
    invalidateMeasure();
}

void UICanvasSlot::setPivot(glm::vec2 value)
{
    if (_pivot == value) {
        return;
    }
    _pivot = value;
    invalidateArrange();
}

void UICanvasSlot::setPreferredSize(glm::vec2 value)
{
    if (_preferredSize == value) {
        return;
    }
    _preferredSize = value;
    invalidateMeasure();
}

void UICanvasSlot::setFixedSize(glm::vec2 value)
{
    if (_fixedSize == value) {
        return;
    }
    _fixedSize = value;
    invalidateArrange();
}

void UICanvasSlot::apply(const FCanvasSlotArgs& args)
{
    setAnchorMin(args.anchorMin);
    setAnchorMax(args.anchorMax);
    setOffset(args.offset);
    setMinSize(args.minSize);
    setMaxSize(args.maxSize);
    setOffsets(args.offsets);
    setAlignmentH(args.alignmentH);
    setAlignmentV(args.alignmentV);
    setWidthSizeMode(args.widthSizeMode);
    setHeightSizeMode(args.heightSizeMode);
    setPivot(args.pivot);
    setPreferredSize(args.preferredSize);
    if (args.fixedSize.x != 0.0f || args.fixedSize.y != 0.0f) {
        setFixedSize(args.fixedSize);
    }
}

void UICanvasSlot::appendRuntimeDiagnostics(nlohmann::json& node) const
{
    node["type"] = "canvas";
    node["anchorMin"] = {_anchorMin.x, _anchorMin.y};
    node["anchorMax"] = {_anchorMax.x, _anchorMax.y};
    node["offset"]    = {_offset.x, _offset.y};
    node["minSize"]   = {_minSize.x, _minSize.y};
    node["maxSize"]   = {_maxSize.x, _maxSize.y};
    node["fixedSize"] = {_fixedSize.x, _fixedSize.y};
}

Rect2D UICanvasLayout::resolveChildRect(const UIElement& child, const UICanvasSlot& slot,
                                        const Rect2D& contentRect)
{
    const glm::vec2 fixed     = slot.getFixedSize();
    const glm::vec2 preferred = slot.getPreferredSize();
    const glm::bvec2 autoAxis{slot.getWidthSizeMode() == EWidgetSizeMode::Auto,
                              slot.getHeightSizeMode() == EWidgetSizeMode::Auto};
    const glm::vec2 authored{autoAxis.x ? 0.0f : (fixed.x != 0.0f ? fixed.x : preferred.x),
                             autoAxis.y ? 0.0f : (fixed.y != 0.0f ? fixed.y : preferred.y)};

    // 1. Anchor area: shared anchor math, so the canvas layout and the legacy
    //    self-positioned path cannot drift. Authored size comes from the slot.
    const Rect2D anchorRect =
        child.resolveCanvasRect(contentRect, slot.getAnchorMin(), slot.getAnchorMax(),
                                slot.getOffset(), slot.getMinSize(), slot.getMaxSize(),
                                authored, autoAxis);

    // 2. Per-edge insets shrink the available area. An inset on an axis also
    //    makes that axis stretch, so "all four edges" means "fill minus insets"
    //    instead of requiring a hand-computed size.
    const FMargin& insets = slot.getOffsets();
    const glm::vec2 insetH{insets.left + insets.right, insets.top + insets.bottom};

    const glm::vec2 anchorSpan =
        (glm::clamp(slot.getAnchorMax(), 0.0f, 1.0f) - glm::clamp(slot.getAnchorMin(), 0.0f, 1.0f)) *
        contentRect.extent;
    const glm::vec2 stretchAxis{anchorSpan.x != 0.0f || insetH.x != 0.0f ? 1.0f : 0.0f,
                                anchorSpan.y != 0.0f || insetH.y != 0.0f ? 1.0f : 0.0f};

    // The area a child may occupy: a stretching axis is bounded by the anchor
    // span, a non-stretching axis keeps the whole parent extent so that
    // alignment has room to move a fixed-size child within it.
    Rect2D area = anchorRect;
    area.pos += glm::vec2{insets.left, insets.top};
    area.extent = glm::max(
        glm::vec2{stretchAxis.x != 0.0f ? anchorRect.extent.x : contentRect.extent.x,
                  stretchAxis.y != 0.0f ? anchorRect.extent.y : contentRect.extent.y} -
            insetH,
        glm::vec2{0.0f, 0.0f});

    // 3. Size resolution per axis: Auto uses the measured desired size, a
    //    stretching axis takes the (inset) area, otherwise the slot's authored
    //    size is kept.
    const glm::vec2 desired = child.computeDesiredSize();
    glm::vec2       size       = anchorRect.extent;
    size.x = slot.getWidthSizeMode() == EWidgetSizeMode::Auto
                 ? (preferred.x != 0.0f ? preferred.x
                                        : desired.x)
             : stretchAxis.x != 0.0f ? area.extent.x
                                     : (fixed.x != 0.0f ? fixed.x : anchorRect.extent.x);
    size.y = slot.getHeightSizeMode() == EWidgetSizeMode::Auto
                 ? (preferred.y != 0.0f ? preferred.y
                                        : desired.y)
             : stretchAxis.y != 0.0f ? area.extent.y
                                     : (fixed.y != 0.0f ? fixed.y : anchorRect.extent.y);
    size = glm::clamp(size, slot.getMinSize(), slot.getMaxSize());

    // 4. Alignment within the available area, then pivot: the resolved position
    //    is where the child's pivot point lands.
    glm::vec2 pos = area.pos;
    switch (slot.getAlignmentH())
    {
        case EWidgetAlignH::Center: pos.x += (area.extent.x - size.x) * 0.5f; break;
        case EWidgetAlignH::Right:  pos.x += area.extent.x - size.x; break;
        case EWidgetAlignH::Left:   break;
    }
    switch (slot.getAlignmentV())
    {
        case EWidgetAlignV::Center: pos.y += (area.extent.y - size.y) * 0.5f; break;
        case EWidgetAlignV::Bottom: pos.y += area.extent.y - size.y; break;
        case EWidgetAlignV::Top:    break;
    }
    pos -= slot.getPivot() * size;
    return Rect2D{.pos = pos, .extent = size};
}

void UICanvasLayout::setPadding(glm::vec2 value)
{
    if (_padding == value) {
        return;
    }
    _padding = value;
    invalidateArrange();
}

std::unique_ptr<UISlot> UICanvasLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UICanvasSlot>(parent, child);
}

glm::vec2 UICanvasLayout::measure(const UIElement& parent) const
{
    glm::vec2 contentExtent = {0.0f, 0.0f};
    for (const auto& child : parent.getChildren()) {
        if (child == nullptr) {
            continue;
        }
        const glm::vec2 desired = resolveDesiredSize(parent, *child);
        contentExtent           = glm::max(contentExtent, desired);
    }
    return contentExtent + _padding * 2.0f;
}

void UICanvasLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    const Rect2D contentRect{.pos = rect.pos + _padding,
                             .extent = glm::max(rect.extent - _padding * 2.0f, glm::vec2{0.0f, 0.0f})};
    for (const auto& childRef : parent.getChildren()) {
        UIElement* child = childRef.get();
        if (child == nullptr) {
            continue;
        }
        const UISlot* edge = parent.getSlotForChild(*child);
        if (const UICanvasSlot* slot = edge ? edge->as<UICanvasSlot>() : nullptr) {
            const Rect2D childRect = resolveChildRect(*child, *slot, contentRect);
            child->layoutAssigned(childRect);
            continue;
        }
        YA_CORE_ERROR("UICanvasLayout: child '{}' has no canvas slot edge", child->_name);
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

void UIBoxSlot::appendRuntimeDiagnostics(nlohmann::json& node) const
{
    node["type"] = "box";
    node["sizeRule"] = _sizeRule == EUIBoxSlotSizeRule::Fill ? "fill" : "auto";
    node["weight"] = _weight;
    node["margin"] = {{"left", _margin.left}, {"top", _margin.top}, {"right", _margin.right}, {"bottom", _margin.bottom}};
    node["crossAlignment"] = static_cast<int>(_crossAlignment);
    node["participatesInLayout"] = _bParticipatesInLayout;
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

void UIBoxSlot::setMargin(FMargin value)
{
    value.left   = std::max(value.left, 0.0f);
    value.top    = std::max(value.top, 0.0f);
    value.right  = std::max(value.right, 0.0f);
    value.bottom = std::max(value.bottom, 0.0f);
    if (_margin != value) {
        _margin = value;
        invalidateMeasure();
    }
}

void UIBoxSlot::apply(const FBoxSlotArgs& args)
{
    setSizeRule(args.sizeRule);
    setWeight(args.weight);
    setMargin(args.margin);
    setCrossAlignment(args.crossAlignment);
    if (args.preferredSize.x != 0.0f || args.preferredSize.y != 0.0f) {
        setPreferredSize(args.preferredSize);
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
    invalidateSubtreePaint();
}

void UILayout::invalidateSubtreePaint() const
{
    if (_owner) {
        _owner->invalidateSubtree();
    }
}

void UILayout::assignChildRect(UIElement& child, const Rect2D& rect) const
{
    child.layoutAssigned(rect);
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
        const FMargin   margin  = slotMargin(parent, child);
        main += (bHorizontal ? desired.x : desired.y) + (bHorizontal ? margin.horizontal() : margin.vertical());
        cross = std::max(cross, (bHorizontal ? desired.y : desired.x) + (bHorizontal ? margin.vertical() : margin.horizontal()));
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
        FMargin          margin{};
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
        const float marginMain  = bHorizontal ? entry.margin.horizontal() : entry.margin.vertical();
        entry.mainExtent = entry.bFill ? entry.mainExtent : desiredMain;
        packedMain += entry.mainExtent + marginMain;
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
        const float marginBefore = bHorizontal ? entry.margin.left : entry.margin.top;
        const float marginAfter  = bHorizontal ? entry.margin.right : entry.margin.bottom;
        const float marginCrossBefore = bHorizontal ? entry.margin.top : entry.margin.left;
        const float marginCross       = bHorizontal ? entry.margin.vertical() : entry.margin.horizontal();
        const float desiredCross = bHorizontal ? entry.desired.y : entry.desired.x;
        const float availableCross = std::max(0.0f, contentCross - marginCross);
        float crossExtent = availableCross;
        float crossPos = (bHorizontal ? content.pos.y : content.pos.x) + marginCrossBefore;

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

        cursor += marginBefore;
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
        assignChildRect(*entry.child, childRect);
        cursor += entry.mainExtent + marginAfter + _spacing;
    }
}

void UISingleChildLayout::setPadding(FMargin value)
{
    value.left   = std::max(value.left, 0.0f);
    value.top    = std::max(value.top, 0.0f);
    value.right  = std::max(value.right, 0.0f);
    value.bottom = std::max(value.bottom, 0.0f);
    if (_padding != value) {
        _padding = value;
        invalidateMeasure();
    }
}

glm::vec2 UISingleChildLayout::measure(const UIElement& parent) const
{
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            return glm::max(resolveDesiredSize(parent, *child) + _padding.size(), glm::vec2(0.0f));
        }
    }
    return glm::max(_padding.size(), glm::vec2(0.0f));
}

std::unique_ptr<UISlot> UISingleChildLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIOverlaySlot>(parent, child);
}

void UISingleChildLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    Rect2D contentRect = rect;
    contentRect.pos += _padding.minOffset();
    contentRect.extent = glm::max(contentRect.extent - _padding.size(), glm::vec2(0.0f));
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        // Fill/Fill is the default and reproduces the pre-slot behaviour
        // exactly; only an explicit align() switches an axis to the child's
        // desired size.
        EUIOverlayAlignment hAlign = EUIOverlayAlignment::Fill;
        EUIOverlayAlignment vAlign = EUIOverlayAlignment::Fill;
        if (const UIOverlaySlot* slot = getSingleChildSlot(parent, *child)) {
            hAlign = slot->getHAlign();
            vAlign = slot->getVAlign();
        }
        const glm::vec2 desired = resolveDesiredSize(parent, *child);
        Rect2D          childRect;
        childRect.pos.x    = overlayAxis(contentRect.pos.x, contentRect.extent.x, desired.x, hAlign);
        childRect.pos.y    = overlayAxis(contentRect.pos.y, contentRect.extent.y, desired.y, vAlign);
        childRect.extent.x = overlayExtent(contentRect.extent.x, desired.x, hAlign);
        childRect.extent.y = overlayExtent(contentRect.extent.y, desired.y, vAlign);
        assignChildRect(*child, childRect);
        return;
    }
}

UIOverlaySlot::UIOverlaySlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UIOverlaySlot::appendRuntimeDiagnostics(nlohmann::json& node) const
{
    auto alignmentName = [](EUIOverlayAlignment value) {
        switch (value) {
        case EUIOverlayAlignment::Fill: return "fill";
        case EUIOverlayAlignment::Start: return "start";
        case EUIOverlayAlignment::Center: return "center";
        case EUIOverlayAlignment::End: return "end";
        }
        return "unknown";
    };
    node["type"] = "overlay";
    node["hAlign"] = alignmentName(_hAlign);
    node["vAlign"] = alignmentName(_vAlign);
    node["padding"] = {{"left", _padding.left}, {"top", _padding.top}, {"right", _padding.right}, {"bottom", _padding.bottom}};
    node["preferredSize"] = {_preferredSize.x, _preferredSize.y};
}

void UIOverlaySlot::setHAlign(EUIOverlayAlignment value)
{
    if (_hAlign != value) {
        _hAlign = value;
        invalidateArrange();
    }
}

void UIOverlaySlot::setVAlign(EUIOverlayAlignment value)
{
    if (_vAlign != value) {
        _vAlign = value;
        invalidateArrange();
    }
}

void UIOverlaySlot::setPadding(FMargin value)
{
    value.left   = std::max(value.left, 0.0f);
    value.top    = std::max(value.top, 0.0f);
    value.right  = std::max(value.right, 0.0f);
    value.bottom = std::max(value.bottom, 0.0f);
    if (_padding != value) {
        _padding = value;
        invalidateMeasure();
    }
}

void UIOverlaySlot::apply(const FOverlaySlotArgs& args)
{
    setHAlign(args.hAlign);
    setVAlign(args.vAlign);
    setPadding(args.padding);
    if (args.preferredSize.x != 0.0f || args.preferredSize.y != 0.0f) {
        setPreferredSize(args.preferredSize);
    }
}

void UIOverlaySlot::setPreferredSize(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_preferredSize != value) {
        _preferredSize = value;
        invalidateMeasure();
    }
}

std::unique_ptr<UISlot> UIOverlayLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIOverlaySlot>(parent, child);
}

glm::vec2 UIOverlayLayout::measure(const UIElement& parent) const
{
    glm::vec2 desired{};
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        glm::vec2 childDesired = resolveDesiredSize(parent, *child);
        if (const UIOverlaySlot* slot = getOverlaySlot(parent, *child)) {
            childDesired += slot->getPadding().size();
        }
        desired = glm::max(desired, childDesired);
    }
    return glm::max(desired, glm::vec2(0.0f));
}

void UIOverlayLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        FMargin padding{};
        EUIOverlayAlignment hAlign = EUIOverlayAlignment::Fill;
        EUIOverlayAlignment vAlign = EUIOverlayAlignment::Fill;
        if (const UIOverlaySlot* slot = getOverlaySlot(parent, *child)) {
            padding = slot->getPadding();
            hAlign  = slot->getHAlign();
            vAlign  = slot->getVAlign();
        }
        Rect2D inner = rect;
        inner.pos += padding.minOffset();
        inner.extent = glm::max(inner.extent - padding.size(), glm::vec2(0.0f));
        const glm::vec2 desired = resolveDesiredSize(parent, *child);
        Rect2D childRect;
        childRect.pos.x    = overlayAxis(inner.pos.x, inner.extent.x, desired.x, hAlign);
        childRect.pos.y    = overlayAxis(inner.pos.y, inner.extent.y, desired.y, vAlign);
        childRect.extent.x = overlayExtent(inner.extent.x, desired.x, hAlign);
        childRect.extent.y = overlayExtent(inner.extent.y, desired.y, vAlign);
        assignChildRect(*child, childRect);
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
        return parent.getLayoutRect().extent;
    }

    glm::vec2 desired{};
    size_t arrangedChildCount = 0;
    for (UIElement* child : children) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 childDesired = resolveDesiredSize(parent, *child);
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

std::unique_ptr<UISlot> UISplitLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIOverlaySlot>(parent, child);
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

    // The split owns the main axis (that is what the ratio and divider decide);
    // only the cross axis honours the pane's slot alignment.
    if (_orientation == ESplitOrientation::Vertical) {
        firstRect  = applyCrossAlign(parent, *children[0], firstRect, false);
        if (children.size() >= 2) {
            secondRect = applyCrossAlign(parent, *children[1], secondRect, false);
        }
    }
    else {
        firstRect = applyCrossAlign(parent, *children[0], firstRect, true);
        if (children.size() >= 2) {
            secondRect = applyCrossAlign(parent, *children[1], secondRect, true);
        }
    }

    assignChildRect(*children[0], firstRect);
    if (children.size() >= 2) {
        assignChildRect(*children[1], secondRect);
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

std::unique_ptr<UISlot> UIScrollLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIOverlaySlot>(parent, child);
}

glm::vec2 UIScrollLayout::measure(const UIElement& parent) const
{
    return parent.getLayoutRect().extent;
}

void UIScrollLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    const auto children = parent.getChildrenInPaintOrder();
    if (children.empty()) {
        _maxScrollOffset = 0.0f;
        _scrollOffset = 0.0f;
        return;
    }

    const glm::vec2 desired = resolveDesiredSize(parent, *children[0]);
    const bool bVertical = _axis == EScrollAxis::Vertical;
    const float contentMain = bVertical ? std::max(desired.y, rect.extent.y)
                                        : std::max(desired.x, rect.extent.x);
    const float viewportMain = bVertical ? rect.extent.y : rect.extent.x;
    const float newMaxOffset = std::max(0.0f, contentMain - viewportMain);
    const float newOffset    = std::clamp(_scrollOffset, 0.0f, newMaxOffset);

    // The scrollbar geometry derives from these values: when they change
    // (content shrank/grew) the owner must re-paint even though its own
    // layout rect did not change — otherwise the incremental paint cache
    // keeps the stale thumb/track (the G2 validation frame catches this).
    if (newMaxOffset != _maxScrollOffset || newOffset != _scrollOffset) {
        _maxScrollOffset = newMaxOffset;
        _scrollOffset    = newOffset;
        invalidateSubtreePaint();
    }

    // Only the cross axis honours the slot alignment: the main axis must stay
    // at the full content extent or scrolling would clip the content.
    Rect2D contentRect = rect;
    if (bVertical) {
        contentRect.pos.y -= _scrollOffset;
        contentRect.extent = {rect.extent.x, contentMain};
    }
    else {
        contentRect.pos.x -= _scrollOffset;
        contentRect.extent = {contentMain, rect.extent.y};
    }
    assignChildRect(*children[0], applyCrossAlign(parent, *children[0], contentRect, !bVertical));
}


// === UITableLayout ===

UITableSlot::UITableSlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UITableSlot::setCell(int row, int column)
{
    _row    = row;
    _column = column;
    invalidateArrange();
}

float UITableLayout::getColumnWidth(int column) const
{
    return column >= 0 && column < static_cast<int>(_columnWidths.size()) ? _columnWidths[column] : 0.0f;
}

void UITableLayout::setColumnCount(int value)
{
    value = std::max(1, value);
    if (value == _columnCount) {
        return;
    }
    _columnCount = value;
    _columnWidths.assign(value, 0.0f);
    invalidateArrange();
}

void UITableLayout::setColumnWidth(int column, float value)
{
    if (column < 0) {
        return;
    }
    if (column >= static_cast<int>(_columnWidths.size())) {
        _columnWidths.resize(column + 1, 0.0f);
    }
    value = std::max(0.0f, value);
    if (_columnWidths[column] == value) {
        return;
    }
    _columnWidths[column] = value;
    invalidateArrange();
}

void UITableLayout::setRowHeight(float value)
{
    value = std::max(1.0f, value);
    if (_rowHeight == value) {
        return;
    }
    _rowHeight = value;
    invalidateArrange();
}

void UITableLayout::setPadding(glm::vec2 value)
{
    if (_padding == value) {
        return;
    }
    _padding = value;
    invalidateArrange();
}

void UITableLayout::setClipsChildren(bool value)
{
    if (_bClipChildren == value) {
        return;
    }
    _bClipChildren = value;
    invalidateSubtreePaint();
}

std::unique_ptr<UISlot> UITableLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UITableSlot>(parent, child);
}

glm::vec2 UITableLayout::measure(const UIElement& parent) const
{
    float totalWidth = _padding.x * 2.0f;
    for (int col = 0; col < _columnCount; ++col) {
        totalWidth += getColumnWidth(col);
    }
    float totalHeight = _padding.y * 2.0f;
    int   maxRow      = 0;
    for (const UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!child->participatesInLayout() || child->getVisibility() == EWidgetVisibility::Hidden) {
            continue;
        }
        const UISlot* edge = parent.getSlotForChild(*child);
        const auto* slot = edge ? edge->as<UITableSlot>() : nullptr;
        if (slot) {
            maxRow = std::max(maxRow, slot->getRow() + 1);
        }
    }
    totalHeight += static_cast<float>(maxRow) * _rowHeight;
    return {totalWidth, totalHeight};
}

void UITableLayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    const glm::vec2 contentPos = rect.pos + _padding;
    const float     contentW   = std::max(0.0f, rect.extent.x - _padding.x * 2.0f);

    // Resolve column rects: fixed widths first, stretch columns share the rest.
    _columnRects.clear();
    _columnRects.reserve(_columnCount);
    float fixedSum = 0.0f;
    int   stretchCount = 0;
    for (int col = 0; col < _columnCount; ++col) {
        const float w = getColumnWidth(col);
        if (w > 0.0f) {
            fixedSum += w;
        }
        else {
            ++stretchCount;
        }
    }
    const float stretchWidth = stretchCount > 0 ? std::max(0.0f, (contentW - fixedSum) / static_cast<float>(stretchCount)) : 0.0f;
    float       cursorX      = contentPos.x;
    for (int col = 0; col < _columnCount; ++col) {
        const float w = getColumnWidth(col) > 0.0f ? getColumnWidth(col) : stretchWidth;
        _columnRects.push_back(Rect2D{.pos = {cursorX, contentPos.y}, .extent = {w, _rowHeight}});
        cursorX += w;
    }

    for (UIElement* child : parent.getChildrenInPaintOrder()) {
        if (!child->participatesInLayout() || child->getVisibility() == EWidgetVisibility::Hidden) {
            continue;
        }
        const UISlot* edge = parent.getSlotForChild(*child);
        const auto* slot = edge ? edge->as<UITableSlot>() : nullptr;
        if (!slot) {
            continue;
        }
        const int col = std::clamp(slot->getColumn(), 0, _columnCount - 1);
        const Rect2D cell{
            .pos    = {_columnRects[col].pos.x, contentPos.y + static_cast<float>(slot->getRow()) * _rowHeight},
            .extent = {_columnRects[col].extent.x, _rowHeight},
        };
        assignChildRect(*child, cell);
    }
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
