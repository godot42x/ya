#include "Scene2D/SpriteAnimationComponent.h"

#include "Core/Common/AssetRef.h"
#include "Core/Common/AssetSlot.h"
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

void SpriteAnimationComponent::syncAtlasImage(const SpriteAnimationSet& set, Sprite2DComponent& sprite)
{
    if (set.atlas.empty()) {
        _bImageSyncValid = false;
        return;
    }

    TextureRef&        image       = sprite.image.textureRef;
    const std::string& currentPath = image.getPath();
    bool               bSamePath   = currentPath == set.atlas;
    if (!bSamePath) {
        bSamePath = currentPath == AssetRefBase::normalizePath(set.atlas);
    }

    const AssetSlot<Texture>* imageSlot       = image._handle.get();
    const uint64_t            imageGeneration = imageSlot ? imageSlot->generation : 0;
    const bool                bSameSlot       = _bImageSyncValid && _syncedImageSlot == imageSlot &&
                                 _syncedImageGeneration == imageGeneration;
    if (bSamePath && bSameSlot) {
        return;
    }
    // First observation of an already-correct path records the slot and does
    // not rebind. A later generation change (hot reload) does.
    if (!bSamePath || (_bImageSyncValid && !bSameSlot)) {
        image.setPath(set.atlas);
        imageSlot = image._handle.get();
    }
    _bImageSyncValid       = true;
    _syncedImageSlot       = imageSlot;
    _syncedImageGeneration = imageSlot ? imageSlot->generation : 0;
}

void SpriteAnimationComponent::showFrame(int32_t frame)
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
    Sprite2DComponent* sprite = owner->tryGetComponent<Sprite2DComponent>();
    if (!sprite) {
        return;
    }
    // Serialized uv windows are rounded. Rewriting an equal window would
    // change the scene file without changing the picture.
    const glm::vec4 delta = glm::abs(sprite->uvRect - rect);
    if (delta.x > 1.0e-4f || delta.y > 1.0e-4f || delta.z > 1.0e-4f || delta.w > 1.0e-4f) {
        sprite->uvRect = rect;
    }
    syncAtlasImage(*set, *sprite);
}

int32_t SpriteAnimationComponent::shownFrame() const
{
    const SpriteAnimationSet* set = animation.get();
    if (!set || _clipIndex < 0 || static_cast<size_t>(_clipIndex) >= set->clips.size()) {
        return -1;
    }
    const SpriteAnimationClip& current = set->clips[static_cast<size_t>(_clipIndex)];
    if (current.frames.empty()) {
        return -1;
    }
    const int32_t count = static_cast<int32_t>(current.frames.size());
    int32_t       index = 0;
    if (current.fps > 0.0f) {
        index = static_cast<int32_t>(std::floor(_elapsed * current.fps));
        index = std::clamp(index, 0, count - 1);
    }
    return current.frames[static_cast<size_t>(index)];
}

void SpriteAnimationComponent::showClipFrame()
{
    const int32_t frame = shownFrame();
    if (frame < 0) {
        return;
    }
    showFrame(frame);
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

void SpriteAnimationComponent::applyDisplayedFrame()
{
    const SpriteAnimationSet*            set   = animation.get();
    const AssetSlot<SpriteAnimationSet>* slot  = animation._handle ? animation._handle.get() : nullptr;
    Entity*                              owner = getOwner();
    Sprite2DComponent*                   sprite = owner ? owner->tryGetComponent<Sprite2DComponent>() : nullptr;
    if (!set || !sprite) {
        // Empty path: nothing to show. A path that has not loaded yet, or a
        // sprite that is deserialized after this component, retries.
        const bool bOweDisplay = animation.hasPath() || set != nullptr;
        _bDisplayPending       = bOweDisplay && (!sprite || !set);
        _bAppliedDisplay       = !_bDisplayPending;
        if (!_bDisplayPending) {
            _appliedSetSlot       = slot;
            _appliedSetGeneration = slot ? slot->generation : 0;
        }
        return;
    }

    _appliedSetSlot       = slot;
    _appliedSetGeneration = slot ? slot->generation : 0;
    _bAppliedDisplay      = true;
    _bDisplayPending      = false;

    int32_t frame = -1;
    if (_clipIndex >= 0) {
        frame = shownFrame();
    }
    if (frame < 0 && !clip.empty()) {
        if (const SpriteAnimationClip* authored = set->findClip(clip)) {
            if (!authored->frames.empty()) {
                frame = authored->frames.front();
            }
        }
    }
    if (frame >= 0) {
        showFrame(frame);
    }
}

void SpriteAnimationComponent::refreshDisplayedFrame()
{
    if (!_bAppliedDisplay && !_bDisplayPending) {
        return;
    }
    if (_bDisplayPending) {
        applyDisplayedFrame();
        return;
    }
    const AssetSlot<SpriteAnimationSet>* slot       = animation._handle ? animation._handle.get() : nullptr;
    const uint64_t                       generation = slot ? slot->generation : 0;
    if (slot == _appliedSetSlot && generation == _appliedSetGeneration) {
        return;
    }
    applyDisplayedFrame();
}

void SpriteAnimationComponent::onEdit()
{
    resetRuntime();
    applyDisplayedFrame();
}

void SpriteAnimationComponent::onPostSerialize()
{
    applyDisplayedFrame();
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
