#pragma once

// ============================================================================
// UIAnimation - framework-layer UI animation (Game UI / UMG split, YA side).
//
// Industry split this file implements (UE Slate vs UMG, Godot Tween vs
// AnimationPlayer, USS transition vs uGUI Animator):
//
//   * Framework layer (THIS FILE): a clock, easing curves and a small set of
//     animatable widget properties. Enough for button feedback, expander,
//     tooltip fade, panel slide. No tracks with keyframes, no clip blending.
//   * Game UI layer (future, document/HUD owned): clip = tracks + keyframes +
//     notifies; it EVALUATES a clip every frame and writes the same animatable
//     properties through the seam below. It is a player on top of this file,
//     never a second invalidation system.
//
// Invariants kept from the GUI invalidation architecture
// (.agent/plan/gui-invalidation-architecture):
//
//   * Animation is a normal invalidation SOURCE, not an exception: every write
//     goes through the widget's changed-only setter, which declares the
//     property's EUIPropertyImpact. No field poking, no _bVolatile, no new
//     dirty channel, no snapshot format change.
//   * Animatable properties are declared by the widget type itself
//     (FUIAnimPropertyTable), so a new animatable property - including one
//     owned by a downstream module's control - is added without modifying the
//     driver (clock / tween / future clip player). That is the open/closed
//     seam: the driver is closed for modification, widget types are open for
//     extension.
//   * A driver only ever sees (widget, propertyId, value). It never needs to
//     know which widget class it is animating.
// ============================================================================

#include "Core/Api.h"
#include "GUI/Widgets/UIBehavior.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace ya
{

struct UIElement;

// === Animatable property seam ===============================================

/// Value domain of an animatable property. Only interpolation-ready domains
/// are listed: a property that cannot be lerped (a resource path, an enum, a
/// subtree shape) is switched/set, not tweened, and is not animatable here.
enum class EUIAnimValueType : uint8_t
{
    Float,
    Vec2,
    Vec4,
};

/// Type-erased animatable value. The type is part of the property declaration;
/// a writer whose value type does not match the declaration is rejected
/// instead of being reinterpreted.
struct FUIAnimValue
{
    EUIAnimValueType type = EUIAnimValueType::Float;
    glm::vec4        data = {0.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] static FUIAnimValue fromFloat(float value);
    [[nodiscard]] static FUIAnimValue fromVec2(const glm::vec2& value);
    [[nodiscard]] static FUIAnimValue fromVec4(const glm::vec4& value);

    [[nodiscard]] float     asFloat() const { return data.x; }
    [[nodiscard]] glm::vec2 asVec2() const { return {data.x, data.y}; }
    [[nodiscard]] glm::vec4 asVec4() const { return data; }
};

/// Component-wise interpolation; t is expected pre-eased. Mismatched value
/// types return the source value (take the safe side: never change domain
/// mid-flight).
[[nodiscard]] YA_GUI_API FUIAnimValue lerpAnimValue(const FUIAnimValue& from,
                                                    const FUIAnimValue& to,
                                                    float               t);

/// One animatable widget property: stable id + value domain + accessors.
///
/// The writer must go through the widget's changed-only setter, so the
/// property's invalidation impact (GI-104) stays declared in exactly one
/// place - the setter. The descriptor never restates the impact, otherwise
/// the two copies could drift apart.
struct FUIAnimPropertyDesc
{
    std::string_view id{};
    EUIAnimValueType type = EUIAnimValueType::Float;
    /// Current value. Never null in a registered table.
    FUIAnimValue (*read)(const UIElement& owner) = nullptr;
    /// Apply a value through the owning setter. Returns false when the value
    /// type does not match the declaration. Never null in a registered table.
    bool (*write)(UIElement& owner, const FUIAnimValue& value) = nullptr;
};

/// The animatable properties of one widget type, plus the inherited table it
/// extends. A control declares its own entries and points its base link at
/// the table of the type it derives from:
///
///   static const FUIAnimPropertyTable kTable{
///       kEntries, std::size(kEntries), &uiElementAnimatableProperties() };
///
/// Own entries win over inherited ones with the same id (an override).
struct FUIAnimPropertyTable
{
    const FUIAnimPropertyDesc*  entries = nullptr;
    size_t                      count   = 0;
    const FUIAnimPropertyTable* base    = nullptr;
};

/// The base widget's animatable properties: the render-only paint overlay
/// (opacity / renderTranslation / renderScale / tint). Every widget inherits
/// them, so a generic driver can fade, slide or scale ANY widget.
[[nodiscard]] YA_GUI_API const FUIAnimPropertyTable& uiElementAnimatableProperties();

// === Easing =================================================================

enum class EUIAnimEase : uint8_t
{
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InBack,
    OutBack,
    InOutBack,
};

/// Map normalized time 0..1 to an eased amount. Out-of-range input is clamped.
[[nodiscard]] YA_GUI_API float evaluateEase(EUIAnimEase ease, float t);

// === Clock ==================================================================

/// A single 0 -> 1 time driver (UE Slate FCurveSequence analogue): duration,
/// direction, loop and time scale, with no knowledge of what it animates. A
/// behaviour reports wantsTick() while the clock runs, so a finished clock
/// costs nothing: the tree stops visiting the widget instead of rebuilding
/// forever.
class YA_GUI_API UIAnimClock
{
  public:
    void setDuration(float seconds);
    [[nodiscard]] float getDuration() const { return _duration; }

    /// Loop between start and end. A looping clock never reports hasFinished();
    /// it keeps its owner ticking until stop()/pause().
    void setLoop(bool bLoop) { _bLoop = bLoop; }
    [[nodiscard]] bool isLooping() const { return _bLoop; }

    /// 1 = real time, 0 = frozen, negative = reversed. Independent of the
    /// play direction (playReverse uses a negative direction).
    void setTimeScale(float scale) { _timeScale = scale; }
    [[nodiscard]] float getTimeScale() const { return _timeScale; }

    /// Start over at the beginning, running forward.
    void play();
    /// Start over at the end, running backward.
    void playReverse();
    /// Freeze in place (resume() continues from here).
    void pause();
    /// Continue after pause() (no-op while already playing).
    void resume();
    /// Stop and reset to the beginning.
    void stop();

    [[nodiscard]] bool  isPlaying() const { return _bPlaying; }
    /// Normalized timeline position 0..1, direction-independent.
    [[nodiscard]] float getLerp() const;
    /// True once a non-looping clock reached the end (cleared by play*()).
    [[nodiscard]] bool hasFinished() const { return _bFinished; }

    /// Advance by real seconds (the host's frame delta). Returns true while
    /// the clock is still running.
    bool tick(float deltaSeconds);

  private:
    float _duration  = 0.25f;
    float _position  = 0.0f;
    float _timeScale = 1.0f;
    /// +1 forward, -1 backward.
    float _direction = 1.0f;
    bool  _bPlaying  = false;
    bool  _bLoop     = false;
    bool  _bFinished = false;
};

// === Tween ==================================================================

/// One animated channel of a tween: this widget's <id> goes from -> to.
struct FUIAnimTrack
{
    std::string  id;   ///< animatable property id (FUIAnimPropertyDesc::id)
    FUIAnimValue from{};
    FUIAnimValue to{};
    EUIAnimEase  ease = EUIAnimEase::Linear;
};

/// Framework-layer tween behaviour: one clock driving N animatable properties
/// of the behaviour's OWNER widget. Multi-widget orchestration (a track bound
/// to a widget by name, keyframe events, clip sequences) belongs to the Game
/// UI layer on top, not here.
///
/// Usage:
///
///   auto tween = std::make_shared<ya::UITweenBehavior>();
///   tween->addFloatTrack("opacity", 0.0f, 1.0f, ya::EUIAnimEase::OutCubic);
///   tween->addVec2Track("renderScale", {0.9f, 0.9f}, {1.0f, 1.0f},
///                       ya::EUIAnimEase::OutBack);
///   tween->setDuration(0.25f);
///   card->addBehavior(tween);
///   tween->play();
class YA_GUI_API UITweenBehavior : public UIBehavior
{
  public:
    // === Authoring ===
    void addFloatTrack(std::string id, float from, float to, EUIAnimEase ease = EUIAnimEase::Linear);
    void addVec2Track(std::string id, glm::vec2 from, glm::vec2 to, EUIAnimEase ease = EUIAnimEase::Linear);
    void addVec4Track(std::string id, glm::vec4 from, glm::vec4 to, EUIAnimEase ease = EUIAnimEase::Linear);
    /// Tracks are resolved against the owner's animatable property table when
    /// the tween runs; a track whose id the owner does not expose is skipped
    /// (warned once per id). A tween never silently animates nothing.
    [[nodiscard]] size_t getTrackCount() const { return _tracks.size(); }
    [[nodiscard]] const FUIAnimTrack& getTrack(size_t index) const { return _tracks[index]; }
    void clearTracks();

    void setDuration(float seconds) { _clock.setDuration(seconds); }
    [[nodiscard]] float getDuration() const { return _clock.getDuration(); }
    void setLoop(bool bLoop) { _clock.setLoop(bLoop); }
    void setTimeScale(float scale) { _clock.setTimeScale(scale); }
    /// Called when a non-looping tween reaches its end (after the final value
    /// was applied). Use it to chain the next step.
    void setOnFinished(std::function<void()> callback) { _onFinished = std::move(callback); }

    // === Playback ===
    /// Apply t=0 and run forward. Replaying a finished tween restarts it.
    void play();
    /// Apply t=1 and run backward.
    void playReverse();
    void stop();
    [[nodiscard]] bool  isPlaying() const { return _clock.isPlaying(); }
    [[nodiscard]] float getLerp() const { return _clock.getLerp(); }

    // === UIBehavior ===
    [[nodiscard]] bool wantsTick() const override;
    void tick(UIElement& owner, float deltaSeconds) override;
    void onDetached(UIElement& owner) override;

  private:
    void applyTracks(UIElement& owner, float lerp);

    std::vector<FUIAnimTrack>  _tracks;
    UIAnimClock                _clock;
    std::function<void()>      _onFinished;
    /// Ids already warned about in this tween run (cleared on play*()).
    std::vector<std::string>   _warnedIds;
    bool                       _bFinishedFired = false;
};

} // namespace ya
