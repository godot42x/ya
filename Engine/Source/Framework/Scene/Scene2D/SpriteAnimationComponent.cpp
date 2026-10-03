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
    if (!isValid() || frame < 0 || frame >= columns * rows) {
        return glm::vec4(0.0f);
    }
    const int32_t column = frame % columns;
    const int32_t row    = frame / columns;
    const float   cw     = 1.0f / static_cast<float>(columns);
    const float   rh     = 1.0f / static_cast<float>(rows);
    return glm::vec4(static_cast<float>(column) * cw, static_cast<float>(row) * rh,
                     static_cast<float>(column + 1) * cw, static_cast<float>(row + 1) * rh);
}

int32_t SpriteAnimationComponent::findClip(const std::string& name) const
{
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

std::string SpriteAnimationComponent::currentClip() const
{
    return _clipIndex >= 0 && static_cast<size_t>(_clipIndex) < clips.size() ? clips[_clipIndex].name : std::string();
}

void SpriteAnimationComponent::showFrame(int32_t frame) const
{
    const glm::vec4 rect = frameRect(frame);
    if (rect.z <= rect.x) {
        // Outside the grid: leave the sprite on its last window rather than
        // draw nothing, and say so, because it is an authoring mistake.
        YA_CORE_WARN("SpriteAnimation: frame {} is outside the {}x{} sheet", frame, columns, rows);
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
    if (_clipIndex < 0 || static_cast<size_t>(_clipIndex) >= clips.size()) {
        return;
    }
    const SpriteAnimationClip& current = clips[_clipIndex];
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
    const int32_t index = findClip(name);
    if (index < 0) {
        YA_CORE_WARN("SpriteAnimation: no clip named '{}'", name);
        return false;
    }
    _bStarted = true;
    if (index == _clipIndex && _bPlaying) {
        return true;
    }
    _clipIndex = index;
    _elapsed   = 0.0f;
    _bPlaying  = true;
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
    if (!_bPlaying || _clipIndex < 0 || static_cast<size_t>(_clipIndex) >= clips.size()) {
        return;
    }
    const SpriteAnimationClip& current = clips[_clipIndex];
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

} // namespace ya
