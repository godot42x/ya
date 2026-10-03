#include "Scene2D/SpriteAnimationComponent.h"

#include "Core/Log.h"
#include "ECS/Entity.h"
#include "Scene2D/Sprite2DComponent.h"

#include <algorithm>
#include <cmath>

namespace ya
{

glm::vec4 SpriteAnimationComponent::frameRect(int32_t frame) const
{
    const SpriteAnimationSet* set = animation.get();
    return set ? set->frameRect(frame) : glm::vec4(0.0f);
}

std::string SpriteAnimationComponent::currentClip() const
{
    return _playingClip;
}

void SpriteAnimationComponent::warnUnloadedOnce()
{
    if (_bWarnedUnloaded) {
        return;
    }
    _bWarnedUnloaded = true;
    YA_CORE_WARN("SpriteAnimation: animation asset is not loaded");
}

bool SpriteAnimationComponent::resolvePlayingClip()
{
    const SpriteAnimationSet*            set        = animation.get();
    const AssetSlot<SpriteAnimationSet>* slot       = animation._handle ? animation._handle.get() : nullptr;
    const uint64_t                       generation = slot ? slot->generation : 0;
    if (set && _resolvedSlot == slot && _resolvedGeneration == generation && _clipIndex >= 0 &&
        static_cast<size_t>(_clipIndex) < set->clips.size() &&
        set->clips[static_cast<size_t>(_clipIndex)].name == _playingClip) {
        return true;
    }

    _resolvedSlot       = slot;
    _resolvedGeneration = generation;
    _clipIndex          = -1;
    if (!set || _playingClip.empty()) {
        return false;
    }
    for (size_t i = 0; i < set->clips.size(); ++i) {
        if (set->clips[i].name == _playingClip) {
            _clipIndex = static_cast<int32_t>(i);
            return true;
        }
    }
    // The name is gone after a reload. Drop it instead of reading off the end.
    _playingClip.clear();
    _bPlaying = false;
    return false;
}

void SpriteAnimationComponent::showFrame(int32_t frame) const
{
    const SpriteAnimationSet* set = animation.get();
    if (!set) {
        return;
    }
    const glm::vec4 rect = set->frameRect(frame);
    if (rect.z <= rect.x) {
        // Outside the grid: leave the sprite on its last window rather than
        // draw nothing, and say so, because it is an authoring mistake.
        YA_CORE_WARN("SpriteAnimation: frame {} is outside the {}x{} sheet", frame, set->columns, set->rows);
        return;
    }
    Entity* owner = getOwner();
    if (!owner) {
        return;
    }
    if (auto* sprite = owner->getComponent<Sprite2DComponent>()) {
        sprite->uvRect = rect;
    }
}

void SpriteAnimationComponent::showClipFrame() const
{
    const SpriteAnimationSet* set = animation.get();
    if (!set || _clipIndex < 0 || static_cast<size_t>(_clipIndex) >= set->clips.size()) {
        return;
    }
    const SpriteAnimationClip& current = set->clips[static_cast<size_t>(_clipIndex)];
    if (current.frames.empty()) {
        return;
    }
    const int32_t count = static_cast<int32_t>(current.frames.size());
    int32_t       index = 0;
    if (current.fps > 0.0f) {
        index = static_cast<int32_t>(std::floor(_elapsed * current.fps));
        index = std::clamp(index, 0, count - 1);
    }
    showFrame(current.frames[static_cast<size_t>(index)]);
}

bool SpriteAnimationComponent::play(const std::string& name)
{
    const SpriteAnimationSet* set = animation.get();
    if (!set) {
        warnUnloadedOnce();
        return false;
    }
    _bWarnedUnloaded = false;

    const SpriteAnimationClip* found = set->findClip(name);
    if (!found) {
        YA_CORE_WARN("SpriteAnimation: no clip named '{}'", name);
        return false;
    }

    _bStarted                                            = true;
    const AssetSlot<SpriteAnimationSet>* slot            = animation._handle.get();
    const uint64_t                       generation      = slot ? slot->generation : 0;
    const bool                           bSameGeneration = _resolvedSlot == slot && _resolvedGeneration == generation;
    if (_bPlaying && _playingClip == name && bSameGeneration) {
        return true;
    }

    // Same name already running across a reload: keep the playhead, retarget
    // the index. A different name starts over.
    const bool bContinue = _bPlaying && _playingClip == name;
    _playingClip         = name;
    _resolvedSlot        = slot;
    _resolvedGeneration  = generation;
    _clipIndex           = -1;
    for (size_t i = 0; i < set->clips.size(); ++i) {
        if (set->clips[i].name == name) {
            _clipIndex = static_cast<int32_t>(i);
            break;
        }
    }
    if (!bContinue) {
        _elapsed = 0.0f;
    }
    _bPlaying = true;
    showClipFrame();
    return true;
}

void SpriteAnimationComponent::stop()
{
    _bStarted = true;
    _bPlaying = false;
}

void SpriteAnimationComponent::setFrame(int32_t frame)
{
    _bStarted  = true;
    _bPlaying  = false;
    _clipIndex = -1;
    _playingClip.clear();
    _resolvedSlot       = nullptr;
    _resolvedGeneration = 0;
    showFrame(frame);
}

void SpriteAnimationComponent::advance(float deltaSeconds)
{
    if (!_bStarted) {
        _bStarted = true;
        if (!clip.empty()) {
            play(clip);
        }
    }
    if (!_bPlaying) {
        return;
    }
    if (!animation.isLoaded()) {
        warnUnloadedOnce();
        return;
    }
    _bWarnedUnloaded = false;
    if (!resolvePlayingClip()) {
        return;
    }

    const SpriteAnimationSet*  set     = animation.get();
    const SpriteAnimationClip& current = set->clips[static_cast<size_t>(_clipIndex)];
    if (current.frames.empty() || current.fps <= 0.0f) {
        _bPlaying = false;
        return;
    }
    const float length = static_cast<float>(current.frames.size()) / current.fps;
    _elapsed += deltaSeconds;
    if (_elapsed >= length) {
        if (current.bLoop) {
            _elapsed = std::fmod(_elapsed, length);
        }
        else {
            // Hold the last frame; `_elapsed` stays on it.
            _elapsed  = length;
            _bPlaying = false;
            showFrame(current.frames.back());
            return;
        }
    }
    showClipFrame();
}

void SpriteAnimationComponent::onEdit()
{
    resetRuntime();
}

void SpriteAnimationComponent::resetRuntime()
{
    _playingClip.clear();
    _clipIndex          = -1;
    _resolvedGeneration = 0;
    _resolvedSlot       = nullptr;
    _elapsed            = 0.0f;
    _bPlaying           = false;
    _bStarted           = false;
    _bWarnedUnloaded    = false;
}

} // namespace ya
