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

const UIContentSlot* getContentSlot(const UIElement& parent, const UIElement& child)
{
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UIContentSlot>() : nullptr;
}

const UICanvasSlot* getCanvasSlot(const UIElement& parent, const UIElement& child)
{
    const UISlot* edge = parent.getSlotForChild(child);
    return edge ? edge->as<UICanvasSlot>() : nullptr;
}

nlohmann::json marginJson(const FMargin& m)
{
    return {{"left", m.left}, {"top", m.top}, {"right", m.right}, {"bottom", m.bottom}};
}

FMargin marginFromJson(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return {};
    }
    return FMargin(j.value("left", 0.0f), j.value("top", 0.0f),
                   j.value("right", 0.0f), j.value("bottom", 0.0f));
}

nlohmann::json vecJson(glm::vec2 v)
{
    return {v.x, v.y};
}

glm::vec2 vecFromJson(const nlohmann::json& j, glm::vec2 fallback = {})
{
    return j.is_array() && j.size() == 2 ? glm::vec2(j[0].get<float>(), j[1].get<float>()) : fallback;
}

/// Per-axis placement inside a box the parent already owns: Fill stretches,
/// anything else keeps the child's desired size and places it. Shared by
/// overlay stacking and content-region hosts.
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

const UIContentSlot* getSingleChildSlot(const UIElement& parent, const UIElement& child)
{
    return getContentSlot(parent, child);
}

FMargin overlaySlotPadding(const UIElement& parent, const UIElement& child)
{
    if (const UIContentSlot* slot = getSingleChildSlot(parent, child)) {
        const FMargin padding = slot->getPadding();
        return {
            std::max(padding.left, 0.0f),
            std::max(padding.top, 0.0f),
            std::max(padding.right, 0.0f),
            std::max(padding.bottom, 0.0f),
        };
    }
    return {};
}

Rect2D insetRectByPadding(const Rect2D& rect, FMargin padding)
{
    Rect2D inner = rect;
    inner.pos += padding.minOffset();
    inner.extent = glm::max(inner.extent - padding.size(), glm::vec2(0.0f));
    return inner;
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
    else if (const UIContentSlot* slot = getContentSlot(parent, child)) {
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
    const Rect2D paddedRect = insetRectByPadding(rect, overlaySlotPadding(parent, child));
    EUIOverlayAlignment crossAlign = EUIOverlayAlignment::Fill;
    if (const UIContentSlot* slot = getSingleChildSlot(parent, child)) {
        crossAlign = bCrossIsY ? slot->getVAlign() : slot->getHAlign();
    }
    if (crossAlign == EUIOverlayAlignment::Fill) {
        return paddedRect;
    }
    const glm::vec2 desired = resolveDesiredSize(parent, child);
    Rect2D          result  = paddedRect;
    if (bCrossIsY) {
        result.pos.y    = overlayAxis(paddedRect.pos.y, paddedRect.extent.y, desired.y, crossAlign);
        result.extent.y = overlayExtent(paddedRect.extent.y, desired.y, crossAlign);
    }
    else {
        result.pos.x    = overlayAxis(paddedRect.pos.x, paddedRect.extent.x, desired.x, crossAlign);
        result.extent.x = overlayExtent(paddedRect.extent.x, desired.x, crossAlign);
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

void UISlot::serialize(nlohmann::json& node) const
{
    node["type"] = "base";
}

void UISlot::deserialize(const nlohmann::json&)
{
}

bool UISlot::isAutoSizeActive() const
{
    return false;
}

void UISlot::invalidateMeasure() const
{
    _parent->markLayoutDirty(EUIInvalidationReason::LayoutProperty);
    if (WidgetTree* tree = _parent->getTree()) {
        tree->invalidateLayout(EWidgetLayoutInvalidation::Measure);
    }
}

UICanvasSlot::UICanvasSlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UICanvasSlot::promoteStretchedAutoAxes()
{
    const bool bSpanX = _anchorMax.x != _anchorMin.x || _offsets.left + _offsets.right != 0.0f;
    const bool bSpanY = _anchorMax.y != _anchorMin.y || _offsets.top + _offsets.bottom != 0.0f;
    if (bSpanX && _widthSizeMode == EWidgetSizeMode::Auto) {
        _widthSizeMode = EWidgetSizeMode::Fixed;
    }
    if (bSpanY && _heightSizeMode == EWidgetSizeMode::Auto) {
        _heightSizeMode = EWidgetSizeMode::Fixed;
    }
}

void UICanvasSlot::setAnchorMin(glm::vec2 value)
{
    if (_anchorMin == value) {
        return;
    }
    _anchorMin = value;
    promoteStretchedAutoAxes();
    invalidateArrange();
}

void UICanvasSlot::setAnchorMax(glm::vec2 value)
{
    if (_anchorMax == value) {
        return;
    }
    _anchorMax = value;
    promoteStretchedAutoAxes();
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
    promoteStretchedAutoAxes();
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
    const bool sizeChanged = _fixedSize != value;
    _fixedSize = value;
    if (_widthSizeMode != EWidgetSizeMode::Fixed) {
        _widthSizeMode = EWidgetSizeMode::Fixed;
    }
    if (_heightSizeMode != EWidgetSizeMode::Fixed) {
        _heightSizeMode = EWidgetSizeMode::Fixed;
    }
    if (sizeChanged) {
        invalidateArrange();
    }
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
        // setFixedSize() is also the imperative convenience API and promotes
        // the slot to Fixed. Construct-time args, however, may intentionally
        // combine fixedSize storage with an Auto axis, so restore the authored
        // per-axis modes after applying the payload.
        setWidthSizeMode(args.widthSizeMode);
        setHeightSizeMode(args.heightSizeMode);
    }
}

FCanvasSlotArgs UICanvasSlot::toArgs() const
{
    return FCanvasSlotArgs{
        .anchorMin      = _anchorMin,
        .anchorMax      = _anchorMax,
        .offset         = _offset,
        .minSize        = _minSize,
        .maxSize        = _maxSize,
        .offsets        = _offsets,
        .alignmentH     = _alignmentH,
        .alignmentV     = _alignmentV,
        .widthSizeMode  = _widthSizeMode,
        .heightSizeMode = _heightSizeMode,
        .pivot          = _pivot,
        .preferredSize  = _preferredSize,
        .fixedSize      = _fixedSize,
    };
}

void UICanvasSlot::assign(const FCanvasSlotArgs& args)
{
    apply(args);
    if (_fixedSize != args.fixedSize) {
        _fixedSize = args.fixedSize;
        invalidateArrange();
    }
}

FCanvasSlotArgs withCanvasAnchorPreset(FCanvasSlotArgs args, ECanvasAnchorPreset preset, glm::vec2 size)
{
    args.offset         = {0.0f, 0.0f};
    args.offsets        = FMargin{};
    args.alignmentH     = EWidgetAlignH::Left;
    args.alignmentV     = EWidgetAlignV::Top;
    args.widthSizeMode  = EWidgetSizeMode::Fixed;
    args.heightSizeMode = EWidgetSizeMode::Fixed;
    args.fixedSize      = size;
    switch (preset) {
    case ECanvasAnchorPreset::StretchHorizontal:
        args.anchorMin = {0.0f, 0.5f};
        args.anchorMax = {1.0f, 0.5f};
        args.pivot     = {0.0f, 0.5f};
        break;
    case ECanvasAnchorPreset::StretchVertical:
        args.anchorMin = {0.5f, 0.0f};
        args.anchorMax = {0.5f, 1.0f};
        args.pivot     = {0.5f, 0.0f};
        break;
    case ECanvasAnchorPreset::Fill:
        args.anchorMin = {0.0f, 0.0f};
        args.anchorMax = {1.0f, 1.0f};
        args.pivot     = {0.0f, 0.0f};
        break;
    default: {
        const int       index = static_cast<int>(preset);
        const glm::vec2 point{static_cast<float>(index % 3) * 0.5f, static_cast<float>(index / 3) * 0.5f};
        args.anchorMin = point;
        args.anchorMax = point;
        args.pivot     = point;
        break;
    }
    }
    return args;
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

void UICanvasSlot::serialize(nlohmann::json& node) const
{
    node["type"] = "canvas";
    node["anchorMin"] = vecJson(_anchorMin);
    node["anchorMax"] = vecJson(_anchorMax);
    node["offset"] = vecJson(_offset);
    node["minSize"] = vecJson(_minSize);
    node["maxSize"] = vecJson(_maxSize);
    node["offsets"] = marginJson(_offsets);
    node["alignmentH"] = static_cast<int>(_alignmentH);
    node["alignmentV"] = static_cast<int>(_alignmentV);
    node["widthSizeMode"] = static_cast<int>(_widthSizeMode);
    node["heightSizeMode"] = static_cast<int>(_heightSizeMode);
    node["pivot"] = vecJson(_pivot);
    node["preferredSize"] = vecJson(_preferredSize);
    node["fixedSize"] = vecJson(_fixedSize);
}

void UICanvasSlot::deserialize(const nlohmann::json& node)
{
    FCanvasSlotArgs args;
    args.anchorMin = vecFromJson(node["anchorMin"]);
    args.anchorMax = vecFromJson(node["anchorMax"]);
    args.offset = vecFromJson(node["offset"]);
    args.minSize = vecFromJson(node["minSize"]);
    args.maxSize = vecFromJson(node["maxSize"], args.maxSize);
    args.offsets = marginFromJson(node["offsets"]);
    args.alignmentH = static_cast<EWidgetAlignH>(node.value("alignmentH", 0));
    args.alignmentV = static_cast<EWidgetAlignV>(node.value("alignmentV", 0));
    args.widthSizeMode = static_cast<EWidgetSizeMode>(node.value("widthSizeMode", 0));
    args.heightSizeMode = static_cast<EWidgetSizeMode>(node.value("heightSizeMode", 0));
    args.pivot = vecFromJson(node["pivot"]);
    args.preferredSize = vecFromJson(node["preferredSize"]);
    args.fixedSize = vecFromJson(node["fixedSize"]);
    apply(args);
}

bool UICanvasSlot::isAutoSizeActive() const
{
    return _widthSizeMode == EWidgetSizeMode::Auto || _heightSizeMode == EWidgetSizeMode::Auto;
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

    // 1. Anchor area: canvas anchor math is resolved exclusively from the
    //    parent-owned slot. Authored size comes from the slot.
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
    // maxSize clamps the child, not this area, so a stretch child with a max
    // width can still be centered in the full anchor span.
    const glm::vec2 stretchBound{
        anchorSpan.x != 0.0f ? anchorSpan.x : contentRect.extent.x,
        anchorSpan.y != 0.0f ? anchorSpan.y : contentRect.extent.y,
    };
    area.extent = glm::max(stretchBound - insetH, glm::vec2{0.0f, 0.0f});

    // 3. Size resolution per axis: Auto (preferred else desired) wins over
    //    stretch-to-area, then authored fixed size. DSL spanning `anchor()` /
    //    `insets()` promote Auto to Fixed so those axes stretch; `fill()` is
    //    the same promotion on both axes. Keep Auto only with an explicit
    //    `widthSizeMode(Auto)` / `heightSizeMode(Auto)` after placement
    //    (content-sized child aligned in the stretch area).
    const glm::vec2 desired = child.computeDesiredSize();
    const auto resolveAxis = [](EWidgetSizeMode mode, float preferredValue, float desiredValue,
                                float stretchValue, bool bStretch, float fixedValue, float fallback) {
        if (mode == EWidgetSizeMode::Auto) {
            return preferredValue != 0.0f ? preferredValue : desiredValue;
        }
        if (bStretch) {
            return stretchValue;
        }
        return fixedValue != 0.0f ? fixedValue : fallback;
    };
    glm::vec2 size{
        resolveAxis(slot.getWidthSizeMode(), preferred.x, desired.x, area.extent.x, stretchAxis.x != 0.0f,
                    fixed.x, anchorRect.extent.x),
        resolveAxis(slot.getHeightSizeMode(), preferred.y, desired.y, area.extent.y, stretchAxis.y != 0.0f,
                    fixed.y, anchorRect.extent.y),
    };
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

void UICanvasLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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
    _parent->markArrangeDirty(EUIInvalidationReason::LayoutProperty);
    if (WidgetTree* tree = getParent().getTree()) {
        tree->invalidateLayout(EWidgetLayoutInvalidation::Arrange);
    }
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

void UIBoxSlot::serialize(nlohmann::json& node) const
{
    node["type"] = "box";
    node["sizeRule"] = static_cast<int>(_sizeRule);
    node["weight"] = _weight;
    node["margin"] = marginJson(_margin);
    node["crossAlignment"] = static_cast<int>(_crossAlignment);
    node["minSize"] = vecJson(_minSize);
    node["maxSize"] = vecJson(_maxSize);
    node["preferredSize"] = vecJson(_preferredSize);
    node["participatesInLayout"] = _bParticipatesInLayout;
    node["reserveSpaceWhenHidden"] = _bReserveSpaceWhenHidden;
}

void UIBoxSlot::deserialize(const nlohmann::json& node)
{
    FBoxSlotArgs args;
    args.sizeRule = static_cast<EUIBoxSlotSizeRule>(node.value("sizeRule", 0));
    args.weight = node.value("weight", 1.0f);
    args.margin = marginFromJson(node["margin"]);
    args.crossAlignment = static_cast<EUIBoxSlotCrossAlignment>(node.value("crossAlignment", 0));
    args.preferredSize = vecFromJson(node["preferredSize"]);
    args.minSize = vecFromJson(node["minSize"]);
    args.maxSize = vecFromJson(node["maxSize"], args.maxSize);
    args.participatesInLayout = node.value("participatesInLayout", true);
    args.reserveSpaceWhenHidden = node.value("reserveSpaceWhenHidden", true);
    apply(args);
}

bool UIBoxSlot::isAutoSizeActive() const
{
    return _sizeRule == EUIBoxSlotSizeRule::Auto;
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
    setMinSize(args.minSize);
    setMaxSize(args.maxSize);
    if (args.preferredSize.x != 0.0f || args.preferredSize.y != 0.0f) {
        setPreferredSize(args.preferredSize);
    }
    setParticipatesInLayout(args.participatesInLayout);
    setReserveSpaceWhenHidden(args.reserveSpaceWhenHidden);
}

FBoxSlotArgs UIBoxSlot::toArgs() const
{
    return FBoxSlotArgs{
        .sizeRule               = _sizeRule,
        .weight                 = _weight,
        .margin                 = _margin,
        .crossAlignment         = _crossAlignment,
        .minSize                = _minSize,
        .maxSize                = _maxSize,
        .preferredSize          = _preferredSize,
        .participatesInLayout   = _bParticipatesInLayout,
        .reserveSpaceWhenHidden = _bReserveSpaceWhenHidden,
    };
}

void UIBoxSlot::assign(const FBoxSlotArgs& args)
{
    apply(args);
    setPreferredSize(args.preferredSize);
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

void UILayout::arrange(UIElement& parent, const Rect2D& rect) const
{
    ++_arrangeCount;
    onArrange(parent, rect);
}

void UILayout::invalidateMeasure() const
{
    if (_owner) {
        _owner->markLayoutDirty(EUIInvalidationReason::LayoutProperty);
    }
}

void UILayout::invalidateArrange() const
{
    if (_owner) {
        _owner->markArrangeDirty(EUIInvalidationReason::LayoutProperty);
    }
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

void UIBoxLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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
        if (!child->participatesInLayout()) {
            assignChildRect(*child, Rect2D{.pos = child->getLayoutRect().pos, .extent = {0.0f, 0.0f}});
            continue;
        }
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
            return glm::max(resolveDesiredSize(parent, *child) + _padding.size() + overlaySlotPadding(parent, *child).size(),
                            glm::vec2(0.0f));
        }
    }
    return glm::max(_padding.size(), glm::vec2(0.0f));
}

std::unique_ptr<UISlot> UISingleChildLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIContentSlot>(parent, child);
}

void UISingleChildLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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
        if (const UIContentSlot* slot = getSingleChildSlot(parent, *child)) {
            hAlign = slot->getHAlign();
            vAlign = slot->getVAlign();
        }
        const Rect2D innerRect = insetRectByPadding(contentRect, overlaySlotPadding(parent, *child));
        const glm::vec2 desired = resolveDesiredSize(parent, *child);
        Rect2D          childRect;
        childRect.pos.x    = overlayAxis(innerRect.pos.x, innerRect.extent.x, desired.x, hAlign);
        childRect.pos.y    = overlayAxis(innerRect.pos.y, innerRect.extent.y, desired.y, vAlign);
        childRect.extent.x = overlayExtent(innerRect.extent.x, desired.x, hAlign);
        childRect.extent.y = overlayExtent(innerRect.extent.y, desired.y, vAlign);
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

void UIOverlaySlot::serialize(nlohmann::json& node) const
{
    node["type"] = "overlay";
    node["hAlign"] = static_cast<int>(_hAlign);
    node["vAlign"] = static_cast<int>(_vAlign);
    node["padding"] = marginJson(_padding);
    node["preferredSize"] = vecJson(_preferredSize);
}

void UIOverlaySlot::deserialize(const nlohmann::json& node)
{
    FOverlaySlotArgs args;
    args.hAlign = static_cast<EUIOverlayAlignment>(node.value("hAlign", 0));
    args.vAlign = static_cast<EUIOverlayAlignment>(node.value("vAlign", 0));
    args.padding = marginFromJson(node["padding"]);
    args.preferredSize = vecFromJson(node["preferredSize"]);
    apply(args);
}

bool UIOverlaySlot::isAutoSizeActive() const
{
    return _hAlign != EUIOverlayAlignment::Fill || _vAlign != EUIOverlayAlignment::Fill;
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

FOverlaySlotArgs UIOverlaySlot::toArgs() const
{
    return FOverlaySlotArgs{
        .hAlign        = _hAlign,
        .vAlign        = _vAlign,
        .padding       = _padding,
        .preferredSize = _preferredSize,
    };
}

void UIOverlaySlot::assign(const FOverlaySlotArgs& args)
{
    apply(args);
    setPreferredSize(args.preferredSize);
}

void UIOverlaySlot::setPreferredSize(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_preferredSize != value) {
        _preferredSize = value;
        invalidateMeasure();
    }
}

UIContentSlot::UIContentSlot(UIElement& parent, UIElement& child)
    : UISlot(parent, child)
{
}

void UIContentSlot::appendRuntimeDiagnostics(nlohmann::json& node) const
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
    node["type"] = "content";
    node["hAlign"] = alignmentName(_hAlign);
    node["vAlign"] = alignmentName(_vAlign);
    node["padding"] = {{"left", _padding.left}, {"top", _padding.top}, {"right", _padding.right}, {"bottom", _padding.bottom}};
    node["preferredSize"] = {_preferredSize.x, _preferredSize.y};
}

void UIContentSlot::serialize(nlohmann::json& node) const
{
    node["type"] = "content";
    node["hAlign"] = static_cast<int>(_hAlign);
    node["vAlign"] = static_cast<int>(_vAlign);
    node["padding"] = marginJson(_padding);
    node["preferredSize"] = vecJson(_preferredSize);
}

void UIContentSlot::deserialize(const nlohmann::json& node)
{
    FContentSlotArgs args;
    args.hAlign = static_cast<EUIOverlayAlignment>(node.value("hAlign", 0));
    args.vAlign = static_cast<EUIOverlayAlignment>(node.value("vAlign", 0));
    args.padding = marginFromJson(node["padding"]);
    args.preferredSize = vecFromJson(node["preferredSize"]);
    apply(args);
}

bool UIContentSlot::isAutoSizeActive() const
{
    return _hAlign != EUIOverlayAlignment::Fill || _vAlign != EUIOverlayAlignment::Fill;
}

void UIContentSlot::setHAlign(EUIOverlayAlignment value)
{
    if (_hAlign != value) {
        _hAlign = value;
        invalidateArrange();
    }
}

void UIContentSlot::setVAlign(EUIOverlayAlignment value)
{
    if (_vAlign != value) {
        _vAlign = value;
        invalidateArrange();
    }
}

void UIContentSlot::setPadding(FMargin value)
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

void UIContentSlot::apply(const FContentSlotArgs& args)
{
    setHAlign(args.hAlign);
    setVAlign(args.vAlign);
    setPadding(args.padding);
    if (args.preferredSize.x != 0.0f || args.preferredSize.y != 0.0f) {
        setPreferredSize(args.preferredSize);
    }
}

FContentSlotArgs UIContentSlot::toArgs() const
{
    return FContentSlotArgs{
        .hAlign        = _hAlign,
        .vAlign        = _vAlign,
        .padding       = _padding,
        .preferredSize = _preferredSize,
    };
}

void UIContentSlot::assign(const FContentSlotArgs& args)
{
    apply(args);
    setPreferredSize(args.preferredSize);
}

void UIContentSlot::setPreferredSize(glm::vec2 value)
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

void UIOverlayLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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
        return;
    }
    // Ratio is the divider centre. Pane pixels are `ratio * content ± half
    // divider`, so mins must be converted through that same mapping or a
    // 40px minFirst would only produce ~37px after the divider is carved out.
    const float halfDivider = _dividerThickness * 0.5f;
    const float minRatio =
        std::clamp((_minFirstExtent + halfDivider) / contentExtent, 0.0f, 1.0f);
    const float maxRatio =
        std::clamp(1.0f - (_minSecondExtent + halfDivider) / contentExtent, 0.0f, 1.0f);
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
    return std::make_unique<UIContentSlot>(parent, child);
}

void UISplitLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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

void UIScrollLayout::setCrossAxisUsesDesiredSize(bool value)
{
    if (_bCrossAxisUsesDesiredSize != value) {
        _bCrossAxisUsesDesiredSize = value;
        invalidateArrange();
    }
}

bool UIScrollLayout::scroll(const glm::vec2& wheelDelta)
{
    bool changed = false;
    const float mainDelta = _axis == EScrollAxis::Vertical ? -wheelDelta.y : -wheelDelta.x;
    if (mainDelta != 0.0f && _maxScrollOffset > 0.0f) {
        const float newOffset = std::clamp(_scrollOffset + mainDelta * _scrollStep, 0.0f, _maxScrollOffset);
        if (newOffset != _scrollOffset) {
            _scrollOffset = newOffset;
            changed = true;
        }
    }
    if (_bCrossAxisUsesDesiredSize) {
        const float crossDelta = _axis == EScrollAxis::Vertical ? -wheelDelta.x : -wheelDelta.y;
        if (crossDelta != 0.0f && _maxCrossScrollOffset > 0.0f) {
            const float newOffset = std::clamp(_crossScrollOffset + crossDelta * _scrollStep, 0.0f, _maxCrossScrollOffset);
            if (newOffset != _crossScrollOffset) {
                _crossScrollOffset = newOffset;
                changed = true;
            }
        }
    }
    if (!changed) {
        return false;
    }
    invalidateArrange();
    return true;
}

std::unique_ptr<UISlot> UIScrollLayout::createSlot(UIElement& parent, UIElement& child) const
{
    return std::make_unique<UIContentSlot>(parent, child);
}

glm::vec2 UIScrollLayout::measure(const UIElement& parent) const
{
    return parent.getLayoutRect().extent;
}

void UIScrollLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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
    const float viewportCross = bVertical ? rect.extent.x : rect.extent.y;
    const float desiredCross = bVertical ? desired.x : desired.y;
    const float contentCross = _bCrossAxisUsesDesiredSize ? std::max(viewportCross, desiredCross)
                                                          : viewportCross;
    const float newMaxOffset = std::max(0.0f, contentMain - viewportMain);
    const float newOffset    = std::clamp(_scrollOffset, 0.0f, newMaxOffset);

    // The scrollbar geometry derives from these values: when they change
    // (content shrank/grew) the owner must re-paint even though its own
    // layout rect did not change — otherwise the incremental paint cache
    // keeps the stale thumb/track (the G2 validation frame catches this).
    const float newMaxCross = std::max(0.0f, contentCross - viewportCross);
    const float newCross    = std::clamp(_crossScrollOffset, 0.0f, newMaxCross);
    if (newMaxOffset != _maxScrollOffset || newOffset != _scrollOffset ||
        newMaxCross != _maxCrossScrollOffset || newCross != _crossScrollOffset) {
        _maxScrollOffset      = newMaxOffset;
        _scrollOffset         = newOffset;
        _maxCrossScrollOffset = newMaxCross;
        _crossScrollOffset    = newCross;
        invalidateSubtreePaint();
    }

    // Only the cross axis honours the slot alignment: the main axis must stay
    // at the full content extent or scrolling would clip the content.
    Rect2D contentRect = rect;
    if (bVertical) {
        contentRect.pos.y -= _scrollOffset;
        contentRect.pos.x -= _crossScrollOffset;
        contentRect.extent = {contentCross, contentMain};
    }
    else {
        contentRect.pos.x -= _scrollOffset;
        contentRect.pos.y -= _crossScrollOffset;
        contentRect.extent = {contentMain, contentCross};
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

void UITableSlot::apply(const FTableSlotArgs& args)
{
    setCell(args.row, args.column);
}

FTableSlotArgs UITableSlot::toArgs() const
{
    return FTableSlotArgs{.row = _row, .column = _column};
}

void UITableSlot::assign(const FTableSlotArgs& args)
{
    apply(args);
}

void UITableSlot::serialize(nlohmann::json& node) const
{
    node["type"] = "table";
    node["row"] = _row;
    node["column"] = _column;
}

void UITableSlot::deserialize(const nlohmann::json& node)
{
    apply(FTableSlotArgs{
        .row = node.value("row", 0),
        .column = node.value("column", 0),
    });
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

void UITableLayout::onArrange(UIElement& parent, const Rect2D& rect) const
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

YA_REFLECT_ENUM_BEGIN(ya::EWidgetAlignH)
YA_REFLECT_ENUM_VALUE(Left)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Right)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetAlignV)
YA_REFLECT_ENUM_VALUE(Top)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Bottom)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetBoxLayout)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetMainAxisAlignment)
YA_REFLECT_ENUM_VALUE(Start)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(End)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::ESplitOrientation)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EScrollAxis)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetSizeMode)
YA_REFLECT_ENUM_VALUE(Fixed)
YA_REFLECT_ENUM_VALUE(Auto)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIOverlayAlignment)
YA_REFLECT_ENUM_VALUE(Fill)
YA_REFLECT_ENUM_VALUE(Start)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(End)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIBoxSlotSizeRule)
YA_REFLECT_ENUM_VALUE(Auto)
YA_REFLECT_ENUM_VALUE(Fill)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIBoxSlotCrossAlignment)
YA_REFLECT_ENUM_VALUE(Stretch)
YA_REFLECT_ENUM_VALUE(Start)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(End)
YA_REFLECT_ENUM_END()

YA_REFLECT_BEGIN_EXTERNAL(ya::FMargin)
YA_REFLECT_FIELD(left)
YA_REFLECT_FIELD(top)
YA_REFLECT_FIELD(right)
YA_REFLECT_FIELD(bottom)
YA_REFLECT_END_EXTERNAL()

// Slot args are the authoring form of a parent-owned slot: the designer
// inspector edits a reflected args copy and writes it back with assign().
YA_REFLECT_BEGIN_EXTERNAL(ya::FCanvasSlotArgs)
YA_REFLECT_FIELD(anchorMin)
YA_REFLECT_FIELD(anchorMax)
YA_REFLECT_FIELD(pivot)
YA_REFLECT_FIELD(offset)
YA_REFLECT_FIELD(offsets)
YA_REFLECT_FIELD(alignmentH)
YA_REFLECT_FIELD(alignmentV)
YA_REFLECT_FIELD(widthSizeMode)
YA_REFLECT_FIELD(heightSizeMode)
YA_REFLECT_FIELD(fixedSize)
YA_REFLECT_FIELD(preferredSize)
YA_REFLECT_FIELD(minSize)
YA_REFLECT_FIELD(maxSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FBoxSlotArgs)
YA_REFLECT_FIELD(sizeRule)
YA_REFLECT_FIELD(weight)
YA_REFLECT_FIELD(margin)
YA_REFLECT_FIELD(crossAlignment)
YA_REFLECT_FIELD(preferredSize)
YA_REFLECT_FIELD(minSize)
YA_REFLECT_FIELD(maxSize)
YA_REFLECT_FIELD(participatesInLayout)
YA_REFLECT_FIELD(reserveSpaceWhenHidden)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FOverlaySlotArgs)
YA_REFLECT_FIELD(hAlign)
YA_REFLECT_FIELD(vAlign)
YA_REFLECT_FIELD(padding)
YA_REFLECT_FIELD(preferredSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FContentSlotArgs)
YA_REFLECT_FIELD(hAlign)
YA_REFLECT_FIELD(vAlign)
YA_REFLECT_FIELD(padding)
YA_REFLECT_FIELD(preferredSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTableSlotArgs)
YA_REFLECT_FIELD(row)
YA_REFLECT_FIELD(column)
YA_REFLECT_END_EXTERNAL()
