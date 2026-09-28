#include "GUI/Widgets/Controls/Switch.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

/// The switch owns exactly one animatable property. Declared next to the
/// widget (not in the framework), which is the extension seam: a downstream
/// control adds a channel the drivers know nothing about.
FUIAnimValue readSwitchProgress(const UIElement& owner)
{
    return FUIAnimValue::fromFloat(static_cast<const UISwitch&>(owner).getProgress());
}

bool writeSwitchProgress(UIElement& owner, const FUIAnimValue& value)
{
    if (value.type != EUIAnimValueType::Float) {
        return false;
    }
    static_cast<UISwitch&>(owner).setProgress(value.asFloat());
    return true;
}

const FUIAnimPropertyDesc kSwitchAnimProperties[] = {
    {.id=kAnimSwitchProgress.id, .type=EUIAnimValueType::Float, .read=&readSwitchProgress, .write=&writeSwitchProgress},
};

const FUIAnimPropertyTable kSwitchAnimPropertyTable{
    .entries=kSwitchAnimProperties,
    .count=std::size(kSwitchAnimProperties),
    .base=&uiElementAnimatableProperties(),
};

glm::vec4 brushColorOr(const FBrush& brush, const glm::vec4& fallback)
{
    return brush.isSolid() ? brush.tintColor : fallback;
}

} // namespace

UISwitch::UISwitch(std::string name) : UIElement(std::move(name), "switch")
{
    _hitFilter   = EWidgetHitFilter::Stop;
    _focusPolicy = EWidgetFocusPolicy::Focusable;
    bindHostLayout(_contentLayout);
    syncContentPadding();

    // Idle (wantsTick() == false) until the first toggle.
    _transition = animate(*this, kDefaultTransitionSeconds);
    _transition->track(kAnimSwitchProgress, 0.0f, 1.0f, EUIAnimEase::OutQuad);
}

const FUIAnimPropertyTable* UISwitch::getAnimatableProperties() const
{
    return &kSwitchAnimPropertyTable;
}

void UISwitch::setChecked(bool value)
{
    if (_bChecked == value) {
        return;
    }
    _bChecked = value;
    // _bChecked is reflect-ed (serialization), not a VisualFlag: mark the
    // paint dirty explicitly so the track/knob re-paint with the new target.
    markPaintDirty();
    if (getTransitionSeconds() > 0.0f) {
        _transition->playToward(value ? EUIAnimDirection::Forward : EUIAnimDirection::Backward);
    }
    else if (_progress != (value ? 1.0f : 0.0f)) {
        setProgress(value ? 1.0f : 0.0f);
    }
    if (_onChanged) {
        _onChanged(_bChecked);
    }
}

void UISwitch::setProgress(float value)
{
    value = glm::clamp(value, 0.0f, 1.0f);
    if (_progress == value) {
        return;
    }
    _progress = value;
    // Paint-only: knob position and track colour move; layout and hit testing
    // do not, so a running transition never re-enters the layout pass.
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UISwitch::setTransitionSeconds(float seconds)
{
    seconds = std::max(seconds, 0.0f);
    if (_transition->getDuration() == seconds) {
        return;
    }
    _transition->setDuration(seconds);
    if (seconds <= 0.0f) {
        settleTransition();
    }
}

float UISwitch::getTransitionSeconds() const
{
    return _transition->getDuration();
}

void UISwitch::settleTransition()
{
    _transition->stop();
    _transition->setLerpNow(_bChecked ? 1.0f : 0.0f);
}

void UISwitch::onAttached()
{
    // A switch built (or deserialized) as already-on must paint correctly on
    // its first frame: animate() already attached the tween, so setLerpNow
    // writes progress through the same setter the tick path uses.
    _transition->setLerpNow(_bChecked ? 1.0f : 0.0f);
}

void UISwitch::syncContentPadding()
{
    _contentLayout.setPadding(FMargin{_trackSize.x + _labelSpacing, 0.0f, 0.0f, 0.0f});
}

void UISwitch::applyAssignedLayout(const Rect2D& rect)
{
    syncContentPadding();
    UIElement::applyAssignedLayout(rect);
}

glm::vec2 UISwitch::computeDesiredSize() const
{
    const glm::vec2 measured = _contentLayout.measure(*this);
    return {measured.x, std::max(_trackSize.y, measured.y)};
}

std::unique_ptr<UISlot> UISwitch::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

Rect2D UISwitch::trackRect() const
{
    Rect2D rect;
    rect.extent = _trackSize;
    rect.pos    = _layoutRect.pos;
    rect.pos.y += std::max(0.0f, (_layoutRect.extent.y - _trackSize.y) * 0.5f);
    return rect;
}

Rect2D UISwitch::knobRect() const
{
    const Rect2D track    = trackRect();
    const float  inset    = std::max(1.0f, track.extent.y * 0.12f);
    const float  diameter = std::max(1.0f, track.extent.y - inset * 2.0f);
    const float  travel   = std::max(0.0f, track.extent.x - diameter - inset * 2.0f);

    Rect2D knob;
    knob.extent = glm::vec2(diameter);
    knob.pos    = track.pos + glm::vec2(inset + travel * _progress, inset);
    return knob;
}

void UISwitch::paintSelf(UIFrameBuilder& builder)
{
    const FCheckBoxStyle& style = resolvedStyle();
    const FVisualChrome   chrome = visualChrome(style);
    const EWidgetVisualFlags flags = composeVisualFlags(_bHovered,
                                                        _bPressed,
                                                        false,
                                                        !isEnabledInTree(),
                                                        false,
                                                        false,
                                                        false);

    // Track: the off brush blends into the on (accent) brush while the knob
    // travels, so the colour change reads as part of the same motion.
    const glm::vec4 offColor = brushColorOr(resolveVisualFill(chrome, flags), glm::vec4(0.35f, 0.38f, 0.45f, 1.0f));
    const glm::vec4 onColor  = brushColorOr(style.checkedFill, glm::vec4(0.24f, 0.46f, 0.82f, 1.0f));
    const glm::vec4 trackColor = glm::mix(offColor, onColor, _progress);

    const Rect2D track      = trackRect();
    const float  trackRound = track.extent.y * 0.5f;
    builder.addRoundedRect(track, trackColor, trackRound);

    const Rect2D knob = knobRect();
    builder.addRoundedRect(knob, style.checkColor, knob.extent.y * 0.5f);
}

void UISwitch::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    node["control"] = {
        {"type", "switch"},
        {"checked", _bChecked},
        {"progress", _progress},
    };
}

void UISwitch::toggle()
{
    setChecked(!_bChecked);
}

bool UISwitch::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Space || keyEvent._keyCode == EKey::Enter)) {
            toggle();
            return true;
        }
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (bPointInside || ctx.bViaCapture) {
            toggle();
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

} // namespace ya
