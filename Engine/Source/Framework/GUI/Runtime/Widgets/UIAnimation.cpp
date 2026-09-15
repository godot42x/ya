#include "GUI/Widgets/UIAnimation.h"

#include "GUI/Widgets/UIElement.h"

#include "Core/Log.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace ya
{

// === Values =================================================================

FUIAnimValue FUIAnimValue::fromFloat(float value)
{
    FUIAnimValue out;
    out.type    = EUIAnimValueType::Float;
    out.data.x  = value;
    return out;
}

FUIAnimValue FUIAnimValue::fromVec2(const glm::vec2& value)
{
    FUIAnimValue out;
    out.type     = EUIAnimValueType::Vec2;
    out.data     = {value.x, value.y, 0.0f, 0.0f};
    return out;
}

FUIAnimValue FUIAnimValue::fromVec4(const glm::vec4& value)
{
    FUIAnimValue out;
    out.type = EUIAnimValueType::Vec4;
    out.data = value;
    return out;
}

FUIAnimValue lerpAnimValue(const FUIAnimValue& from, const FUIAnimValue& to, float t)
{
    if (from.type != to.type) {
        return from;
    }
    FUIAnimValue out;
    out.type = from.type;
    out.data = glm::mix(from.data, to.data, t);
    return out;
}

// === Easing =================================================================

namespace
{

float easeInQuad(float t) { return t * t; }
float easeOutQuad(float t) { return t * (2.0f - t); }
float easeInOutQuad(float t)
{
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}
float easeInCubic(float t) { return t * t * t; }
float easeOutCubic(float t)
{
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
float easeInOutCubic(float t)
{
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
float easeInBack(float t)
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}
float easeOutBack(float t)
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float     u  = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
float easeInOutBack(float t)
{
    constexpr float c1 = 1.70158f;
    constexpr float c2 = c1 * 1.525f;
    return t < 0.5f
               ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
               : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) / 2.0f;
}

} // namespace

float evaluateEase(EUIAnimEase ease, float t)
{
    t = glm::clamp(t, 0.0f, 1.0f);
    switch (ease) {
    case EUIAnimEase::InQuad:
        return easeInQuad(t);
    case EUIAnimEase::OutQuad:
        return easeOutQuad(t);
    case EUIAnimEase::InOutQuad:
        return easeInOutQuad(t);
    case EUIAnimEase::InCubic:
        return easeInCubic(t);
    case EUIAnimEase::OutCubic:
        return easeOutCubic(t);
    case EUIAnimEase::InOutCubic:
        return easeInOutCubic(t);
    case EUIAnimEase::InBack:
        return easeInBack(t);
    case EUIAnimEase::OutBack:
        return easeOutBack(t);
    case EUIAnimEase::InOutBack:
        return easeInOutBack(t);
    case EUIAnimEase::Linear:
    default:
        return t;
    }
}

// === Base animatable property table =========================================

namespace
{

FUIAnimValue readRenderOpacity(const UIElement& owner)
{
    return FUIAnimValue::fromFloat(owner.getRenderOpacity());
}

bool writeRenderOpacity(UIElement& owner, const FUIAnimValue& value)
{
    if (value.type != EUIAnimValueType::Float) {
        return false;
    }
    owner.setRenderOpacity(value.asFloat());
    return true;
}

FUIAnimValue readRenderTranslation(const UIElement& owner)
{
    return FUIAnimValue::fromVec2(owner.getRenderTranslation());
}

bool writeRenderTranslation(UIElement& owner, const FUIAnimValue& value)
{
    if (value.type != EUIAnimValueType::Vec2) {
        return false;
    }
    owner.setRenderTranslation(value.asVec2());
    return true;
}

FUIAnimValue readRenderScale(const UIElement& owner)
{
    return FUIAnimValue::fromVec2(owner.getRenderScale());
}

bool writeRenderScale(UIElement& owner, const FUIAnimValue& value)
{
    if (value.type != EUIAnimValueType::Vec2) {
        return false;
    }
    owner.setRenderScale(value.asVec2());
    return true;
}

FUIAnimValue readRenderTint(const UIElement& owner)
{
    return FUIAnimValue::fromVec4(owner.getRenderTint());
}

bool writeRenderTint(UIElement& owner, const FUIAnimValue& value)
{
    if (value.type != EUIAnimValueType::Vec4) {
        return false;
    }
    owner.setRenderTint(value.asVec4());
    return true;
}

const FUIAnimPropertyDesc kBaseAnimProperties[] = {
    {.id="opacity", .type=EUIAnimValueType::Float, .read=&readRenderOpacity, .write=&writeRenderOpacity},
    {.id="renderTranslation", .type=EUIAnimValueType::Vec2, .read=&readRenderTranslation, .write=&writeRenderTranslation},
    {.id="renderScale", .type=EUIAnimValueType::Vec2, .read=&readRenderScale, .write=&writeRenderScale},
    {.id="tint", .type=EUIAnimValueType::Vec4, .read=&readRenderTint, .write=&writeRenderTint},
};

const FUIAnimPropertyTable kBaseAnimPropertyTable{
    .entries=kBaseAnimProperties,
    .count=std::size(kBaseAnimProperties),
    .base=nullptr,
};

const FUIAnimPropertyDesc* findAnimPropertyInTable(const FUIAnimPropertyTable* table,
                                                   std::string_view             id)
{
    for (const FUIAnimPropertyTable* t = table; t != nullptr; t = t->base) {
        for (size_t i = 0; i < t->count; ++i) {
            if (t->entries[i].id == id) {
                return &t->entries[i];
            }
        }
    }
    return nullptr;
}

} // namespace

const FUIAnimPropertyTable& uiElementAnimatableProperties()
{
    return kBaseAnimPropertyTable;
}

const FUIAnimPropertyTable* UIElement::getAnimatableProperties() const
{
    return &kBaseAnimPropertyTable;
}

const FUIAnimPropertyDesc* UIElement::findAnimatableProperty(std::string_view id) const
{
    return findAnimPropertyInTable(getAnimatableProperties(), id);
}

bool UIElement::readAnimatableProperty(std::string_view id, FUIAnimValue& outValue) const
{
    const FUIAnimPropertyDesc* desc = findAnimatableProperty(id);
    if (!desc || !desc->read) {
        return false;
    }
    outValue = desc->read(*this);
    return true;
}

bool UIElement::applyAnimatableProperty(std::string_view id, const FUIAnimValue& value)
{
    const FUIAnimPropertyDesc* desc = findAnimatableProperty(id);
    if (!desc || !desc->write) {
        return false;
    }
    return desc->write(*this, value);
}

std::vector<std::string_view> UIElement::collectAnimatablePropertyIds() const
{
    std::vector<std::string_view> ids;
    for (const FUIAnimPropertyTable* t = getAnimatableProperties(); t != nullptr; t = t->base) {
        for (size_t i = 0; i < t->count; ++i) {
            if (std::find(ids.begin(), ids.end(), t->entries[i].id) == ids.end()) {
                ids.push_back(t->entries[i].id);
            }
        }
    }
    return ids;
}

// === Clock ==================================================================

void UIAnimClock::setDuration(float seconds)
{
    _duration = std::max(seconds, 0.0f);
}

void UIAnimClock::play()
{
    _direction = 1.0f;
    _position  = 0.0f;
    _bFinished = false;
    if (_duration <= 0.0f) {
        _bPlaying  = false;
        _bFinished = true;
        return;
    }
    _bPlaying = true;
}

void UIAnimClock::playReverse()
{
    _direction = -1.0f;
    _position  = _duration;
    _bFinished = false;
    if (_duration <= 0.0f) {
        _bPlaying  = false;
        _bFinished = true;
        return;
    }
    _bPlaying = true;
}

void UIAnimClock::pause()
{
    _bPlaying = false;
}

void UIAnimClock::resume()
{
    if (!_bFinished && _duration > 0.0f) {
        _bPlaying = true;
    }
}

void UIAnimClock::stop()
{
    _bPlaying  = false;
    _direction = 1.0f;
    _position  = 0.0f;
    _bFinished = false;
}

void UIAnimClock::playToward(EUIAnimDirection direction)
{
    if (_duration <= 0.0f) {
        _position  = direction == EUIAnimDirection::Forward ? _duration : 0.0f;
        _bPlaying  = false;
        _bFinished = true;
        return;
    }
    const bool bForward  = direction == EUIAnimDirection::Forward;
    const bool bAtTarget = bForward ? (_position >= _duration) : (_position <= 0.0f);
    if (bAtTarget) {
        // Already there: settle instead of running a no-op animation.
        _bPlaying  = false;
        _bFinished = true;
        return;
    }
    _direction = bForward ? 1.0f : -1.0f;
    _bFinished = false;
    _bPlaying  = true;
}

void UIAnimClock::setLerp(float value)
{
    _position  = glm::clamp(value, 0.0f, 1.0f) * _duration;
    _bPlaying  = false;
    _bFinished = false;
}

float UIAnimClock::getLerp() const
{
    if (_duration <= 0.0f) {
        return 1.0f;
    }
    return glm::clamp(_position / _duration, 0.0f, 1.0f);
}

bool UIAnimClock::tick(float deltaSeconds)
{
    if (!_bPlaying) {
        return false;
    }
    if (_duration <= 0.0f) {
        _bPlaying  = false;
        _bFinished = true;
        return false;
    }

    _position += deltaSeconds * _timeScale * _direction;
    if (_position >= _duration) {
        if (_bLoop) {
            _position = std::fmod(_position, _duration);
        }
        else {
            _position  = _duration;
            _bPlaying  = false;
            _bFinished = true;
            return false;
        }
    }
    else if (_position <= 0.0f) {
        if (_bLoop) {
            _position = _duration - std::fmod(-_position, _duration);
        }
        else {
            _position  = 0.0f;
            _bPlaying  = false;
            _bFinished = true;
            return false;
        }
    }
    return true;
}

// === Tween behaviour ========================================================

UITweenBehavior& UITweenBehavior::addFloatTrack(std::string id, float from, float to, EUIAnimEase ease)
{
    FUIAnimTrack track;
    track.id   = std::move(id);
    track.from = FUIAnimValue::fromFloat(from);
    track.to   = FUIAnimValue::fromFloat(to);
    track.ease = ease;
    _tracks.push_back(std::move(track));
    _resolved.clear();
    return *this;
}

UITweenBehavior& UITweenBehavior::addVec2Track(std::string id, glm::vec2 from, glm::vec2 to, EUIAnimEase ease)
{
    FUIAnimTrack track;
    track.id   = std::move(id);
    track.from = FUIAnimValue::fromVec2(from);
    track.to   = FUIAnimValue::fromVec2(to);
    track.ease = ease;
    _tracks.push_back(std::move(track));
    _resolved.clear();
    return *this;
}

UITweenBehavior& UITweenBehavior::addVec4Track(std::string id, glm::vec4 from, glm::vec4 to, EUIAnimEase ease)
{
    FUIAnimTrack track;
    track.id   = std::move(id);
    track.from = FUIAnimValue::fromVec4(from);
    track.to   = FUIAnimValue::fromVec4(to);
    track.ease = ease;
    _tracks.push_back(std::move(track));
    _resolved.clear();
    return *this;
}

void UITweenBehavior::clearTracks()
{
    _tracks.clear();
    _resolved.clear();
    _clock.stop();
    _warnedIds.clear();
    _bFinishedFired = false;
}

bool UITweenBehavior::wantsTick() const
{
    return _clock.isPlaying();
}

UITweenBehavior& UITweenBehavior::play()
{
    _warnedIds.clear();
    _bFinishedFired = false;
    _clock.play();
    if (UIElement* owner = getOwner()) {
        applyTracks(*owner, 0.0f);
    }
    if (_clock.hasFinished() && !_bFinishedFired) {
        _bFinishedFired = true;
        if (_onFinished) {
            _onFinished();
        }
    }
    return *this;
}

UITweenBehavior& UITweenBehavior::playReverse()
{
    _warnedIds.clear();
    _bFinishedFired = false;
    _clock.playReverse();
    if (UIElement* owner = getOwner()) {
        applyTracks(*owner, 1.0f);
    }
    if (_clock.hasFinished() && !_bFinishedFired) {
        _bFinishedFired = true;
        if (_onFinished) {
            _onFinished();
        }
    }
    return *this;
}

UITweenBehavior& UITweenBehavior::playToward(EUIAnimDirection direction)
{
    _warnedIds.clear();
    _bFinishedFired = false;
    _clock.playToward(direction);
    if (UIElement* owner = getOwner()) {
        // Apply the current (unchanged) position so the first frame after the
        // retarget is already consistent, then let the clock move it.
        applyTracks(*owner, _clock.getLerp());
    }
    settleFinishedCallback();
    return *this;
}

UITweenBehavior& UITweenBehavior::setLerpNow(float lerp)
{
    _warnedIds.clear();
    _clock.setLerp(lerp);
    if (UIElement* owner = getOwner()) {
        applyTracks(*owner, _clock.getLerp());
    }
    _bFinishedFired = false;
    return *this;
}

UITweenBehavior& UITweenBehavior::stop()
{
    _clock.stop();
    return *this;
}

void UITweenBehavior::tick(UIElement& owner, float deltaSeconds)
{
    _clock.tick(deltaSeconds);
    applyTracks(owner, _clock.getLerp());
    settleFinishedCallback();
}

void UITweenBehavior::settleFinishedCallback()
{
    if (_clock.hasFinished() && !_bFinishedFired) {
        _bFinishedFired = true;
        if (_onFinished) {
            _onFinished();
        }
    }
}

void UITweenBehavior::onDetached(UIElement& owner)
{
    _clock.stop();
    // The next owner may expose a different property table.
    _resolved.clear();
    UIBehavior::onDetached(owner);
}

void UITweenBehavior::resolveTrackDescriptors(UIElement& owner)
{
    // Runs once per track set / owner change. Descriptors come from static
    // per-widget-type tables, so the pointers stay valid while the owner (and
    // therefore its table) is unchanged. Resolving here keeps the per-tick
    // path free of string lookups.
    _resolved.assign(_tracks.size(), nullptr);
    for (size_t i = 0; i < _tracks.size(); ++i) {
        const FUIAnimTrack&        track = _tracks[i];
        const FUIAnimPropertyDesc* desc  = owner.findAnimatableProperty(track.id);
        if (!desc || !desc->write) {
            warnTrackSkipped(owner, track, "exposes no animatable property");
            continue;
        }
        if (desc->type != track.from.type || desc->type != track.to.type) {
            warnTrackSkipped(owner, track, "value type does not match the owner's declaration");
            continue;
        }
        _resolved[i] = desc;
    }
}

void UITweenBehavior::warnTrackSkipped(UIElement& owner, const FUIAnimTrack& track, const char* reason)
{
    if (std::ranges::find(_warnedIds, track.id) != _warnedIds.end()) {
        return;
    }
    _warnedIds.push_back(track.id);
    YA_CORE_WARN("UITweenBehavior: owner '{}' {} for track '{}'; track skipped", owner._name, reason, track.id);
}

void UITweenBehavior::applyTracks(UIElement& owner, float lerp)
{
    if (_resolved.size() != _tracks.size()) {
        resolveTrackDescriptors(owner);
    }
    for (size_t i = 0; i < _tracks.size(); ++i) {
        const FUIAnimPropertyDesc* desc = _resolved[i];
        if (!desc) {
            // Unresolved track: already reported once at resolve time.
            continue;
        }
        const FUIAnimTrack& track = _tracks[i];
        desc->write(owner, lerpAnimValue(track.from, track.to, evaluateEase(track.ease, lerp)));
    }
}

// === Authoring entry points =================================================

std::shared_ptr<UITweenBehavior> animate(UIElement& widget, float duration)
{
    auto behavior = std::make_shared<UITweenBehavior>();
    behavior->setDuration(duration);
    widget.addBehavior(behavior);
    return behavior;
}

} // namespace ya
