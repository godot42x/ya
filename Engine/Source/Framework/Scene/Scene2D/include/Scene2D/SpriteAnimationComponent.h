#pragma once

#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace ya
{

// Frame animation for the Sprite2DComponent on the same entity. The sheet
// layout and the named clips live in a shared .yaanim.json
// (`animation`); this component only stores that reference and the clip
// that starts when the game starts running (`clip`).
//
// The component writes `Sprite2DComponent::uvRect` and nothing else, so size,
// tint, flip and sort stay with the sprite. The write happens immediately in
// `play` / `setFrame` (a script sees the new frame in the same tick) and again
// from `advance`, which `SpriteAnimationSystem` calls while the game runs. The
// editor does not advance animations, so an authored `uvRect` stays what the
// scene file says.
//
// A script drives it by name: `anim:play("walk_left")` every tick is fine, a
// clip that is already playing is not restarted. The playing clip is remembered
// by name. A cached sheet index is reused only while the asset slot's
// generation is unchanged; a reload resolves the name again.
struct YA_SCENE_2D_API SpriteAnimationComponent : public IComponent
{
    YA_REFLECT_BEGIN(SpriteAnimationComponent, IComponent)
    YA_REFLECT_FIELD(animation)
    YA_REFLECT_FIELD(clip)
    YA_REFLECT_METHOD(play, .tooltip("Start a clip by name; false when the clip does not exist"))
    YA_REFLECT_METHOD(stop, .tooltip("Hold the current frame"))
    YA_REFLECT_METHOD(setFrame, .tooltip("Show one sheet frame and stop"))
    YA_REFLECT_METHOD(isPlaying, .tooltip("True while a clip is advancing"))
    YA_REFLECT_METHOD(currentClip, .tooltip("Name of the clip last started, empty when none"))
    YA_REFLECT_END()

    SpriteAnimationSetRef animation;
    /// Clip that starts when the game starts running. Empty = none.
    std::string           clip;

    // Runtime state, not serialized. The playing clip is a name; the index
    // is valid only for `_resolvedSlot` at `_resolvedGeneration`.
    std::string                           _playingClip;
    int32_t                               _clipIndex          = -1;
    uint64_t                              _resolvedGeneration = 0;
    const AssetSlot<SpriteAnimationSet>*  _resolvedSlot       = nullptr;
    float                                 _elapsed            = 0.0f;
    bool                                  _bPlaying           = false;
    bool                                  _bStarted           = false;
    bool                                  _bWarnedUnloaded    = false;

    /// Image-space window of one sheet frame: (u0, v0, u1, v1). Forwards to
    /// the asset. An unloaded asset or a frame outside the grid has no window
    /// (zero rect), which nothing draws.
    [[nodiscard]] glm::vec4 frameRect(int32_t frame) const;

    bool                      play(const std::string& name);
    void                      stop();
    void                      setFrame(int32_t frame);
    [[nodiscard]] bool        isPlaying() const { return _bPlaying; }
    [[nodiscard]] std::string currentClip() const;
    /// Sheet index `showClipFrame` would display, or -1 when no clip is resolved.
    [[nodiscard]] int32_t shownFrame() const;

    /// Moves the playhead by `deltaSeconds` and shows the frame it lands on.
    /// The first call starts the authored `clip`. An unloaded asset does nothing.
    void advance(float deltaSeconds);

    /// Inspector edits (a new asset, a new start clip) drop the playhead so
    /// the next `advance` starts `clip` again.
    void onEdit() override;

  private:
    void warnUnloadedOnce();
    /// Point `_clipIndex` at `_playingClip` in the current asset. False when
    /// the asset has no such clip; the index is then left unused.
    bool resolvePlayingClip();
    void showFrame(int32_t frame) const;
    void showClipFrame() const;
    void resetRuntime();
};

} // namespace ya
