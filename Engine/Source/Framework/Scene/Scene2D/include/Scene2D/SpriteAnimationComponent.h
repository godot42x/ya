#pragma once

#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ya
{

// Frame animation for the Sprite2DComponent on the same entity: it chooses
// which window of the sprite's image to show. The image is cut into a uniform
// `columns` x `rows` grid; a frame index is `row * columns + column`, counted
// from the top-left (the image-space convention of `Sprite2DComponent::uvRect`).
//
// The component writes `Sprite2DComponent::uvRect` and nothing else, so size,
// tint, flip and sort stay with the sprite. The write happens immediately in
// `play` / `setFrame` (a script sees the new frame in the same tick) and again
// from `advance`, which `SpriteAnimationSystem` calls while the game runs. The
// editor does not advance animations, so an authored `uvRect` stays what the
// scene file says.
//
// A script drives it by name: `anim:play("walk_left")` every tick is fine, a
// clip that is already playing is not restarted.
struct YA_SCENE_2D_API SpriteAnimationComponent : public IComponent
{
    YA_REFLECT_BEGIN(SpriteAnimationComponent, IComponent)
    YA_REFLECT_FIELD(columns)
    YA_REFLECT_FIELD(rows)
    YA_REFLECT_FIELD(clips)
    YA_REFLECT_FIELD(clip)
    YA_REFLECT_METHOD(play, .tooltip("Start a clip by name; false when the clip does not exist"))
    YA_REFLECT_METHOD(stop, .tooltip("Hold the current frame"))
    YA_REFLECT_METHOD(setFrame, .tooltip("Show one sheet frame and stop"))
    YA_REFLECT_METHOD(isPlaying, .tooltip("True while a clip is advancing"))
    YA_REFLECT_METHOD(currentClip, .tooltip("Name of the clip last started, empty when none"))
    YA_REFLECT_END()

    int32_t                          columns = 1;
    int32_t                          rows    = 1;
    std::vector<SpriteAnimationClip> clips;
    /// Clip that starts when the game starts running. Empty = none.
    std::string                      clip;

    // Runtime state, not serialized.
    int32_t _clipIndex = -1;
    float   _elapsed   = 0.0f;
    bool    _bPlaying  = false;
    bool    _bStarted  = false;

    [[nodiscard]] bool isValid() const { return columns > 0 && rows > 0; }

    /// Image-space window of one sheet frame: (u0, v0, u1, v1). A frame outside
    /// the grid has no window (zero rect), which nothing draws.
    [[nodiscard]] glm::vec4 frameRect(int32_t frame) const;
    [[nodiscard]] int32_t   findClip(const std::string& name) const;

    bool        play(const std::string& name);
    void        stop();
    void        setFrame(int32_t frame);
    [[nodiscard]] bool        isPlaying() const { return _bPlaying; }
    [[nodiscard]] std::string currentClip() const;

    /// Moves the playhead by `deltaSeconds` and shows the frame it lands on.
    /// The first call starts the authored `clip`.
    void advance(float deltaSeconds);

  private:
    void showFrame(int32_t frame) const;
    void showClipFrame() const;
};

} // namespace ya
