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
#include <type_traits>
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

/// Easing curve of one tween segment (or of one keyframe segment).
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

/// One keyframe of a property curve. `time` is normalized clock time (0..1,
/// NOT seconds), so a curve is retimed by setDuration() like any other track;
/// `ease` describes the segment ENDING at this key.
struct FUIAnimKey
{
    float        time  = 0.0f;
    FUIAnimValue value{};
    EUIAnimEase  ease = EUIAnimEase::Linear;
};

[[nodiscard]] YA_GUI_API FUIAnimKey animKey(float time, float value, EUIAnimEase ease = EUIAnimEase::Linear);
[[nodiscard]] YA_GUI_API FUIAnimKey animKey(float time, glm::vec2 value, EUIAnimEase ease = EUIAnimEase::Linear);
[[nodiscard]] YA_GUI_API FUIAnimKey animKey(float time, glm::vec4 value, EUIAnimEase ease = EUIAnimEase::Linear);

/// Evaluate a keyframe curve at normalized clock time `lerp`. Before the first
/// and after the last key the curve holds that key's value (no extrapolation);
/// a single key is a constant. Keys must be sorted by time and share one value
/// domain - a track declares that, and a curve that violates it is rejected when
/// the track is resolved instead of being evaluated into garbage.
[[nodiscard]] YA_GUI_API FUIAnimValue evaluateAnimCurve(const std::vector<FUIAnimKey>& keys, float lerp);

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

/// The base widget's animatable properties: the render transform
/// (opacity / renderTranslation / renderScale / tint). Every widget inherits
/// them, so a generic driver can fade, slide or scale ANY widget.
[[nodiscard]] YA_GUI_API const FUIAnimPropertyTable& uiElementAnimatableProperties();

// === Typed property handles =================================================

/// Compile-time handle for one animatable property. The value domain is part
/// of the type, so a caller cannot pass a vec2 where the widget declares a
/// float. A widget type that declares its own property publishes a handle next
/// to its table, and any driver - the framework tween or a future Game UI clip
/// player - can then drive it without knowing the widget class:
///
///   inline constexpr ya::TUIAnimProperty<float> kAnimGauge{"gauge"};
///   tween->track(kAnimGauge, 0.0f, 1.0f);
template <typename T>
struct TUIAnimProperty
{
    std::string_view id;
};

/// Value domain of a typed property / track value.
template <typename T>
struct TUIAnimValueDomain;
template <>
struct TUIAnimValueDomain<float>
{
    static constexpr EUIAnimValueType value = EUIAnimValueType::Float;
};
template <>
struct TUIAnimValueDomain<glm::vec2>
{
    static constexpr EUIAnimValueType value = EUIAnimValueType::Vec2;
};
template <>
struct TUIAnimValueDomain<glm::vec4>
{
    static constexpr EUIAnimValueType value = EUIAnimValueType::Vec4;
};

template <typename T>
[[nodiscard]] constexpr EUIAnimValueType animValueTypeOf()
{
    return TUIAnimValueDomain<T>::value;
}

inline constexpr TUIAnimProperty<float>     kAnimOpacity{"opacity"};
inline constexpr TUIAnimProperty<glm::vec2> kAnimRenderTranslation{"renderTranslation"};
inline constexpr TUIAnimProperty<glm::vec2> kAnimRenderScale{"renderScale"};
inline constexpr TUIAnimProperty<glm::vec4> kAnimTint{"tint"};

// Short names for authoring chains (`tween->track(ya::ui::anim::opacity, ...)`).
namespace ui::anim
{
inline constexpr TUIAnimProperty<float>     opacity     = kAnimOpacity;
inline constexpr TUIAnimProperty<glm::vec2> translation = kAnimRenderTranslation;
inline constexpr TUIAnimProperty<glm::vec2> scale       = kAnimRenderScale;
inline constexpr TUIAnimProperty<glm::vec4> tint        = kAnimTint;
} // namespace ui::anim

// === Easing =================================================================

/// Map normalized time 0..1 to an eased amount. Out-of-range input is clamped.
[[nodiscard]] YA_GUI_API float evaluateEase(EUIAnimEase ease, float t);

// === Clock ==================================================================

/// Endpoint a clock is asked to move toward. Directional playback continues
/// from the CURRENT position, so retargeting mid-flight (hover in/out, a knob
/// toggled twice quickly) never snaps to an endpoint.
enum class EUIAnimDirection : uint8_t
{
    Forward,  ///< toward t = 1
    Backward, ///< toward t = 0
};

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
    /// Continue from the current position toward an endpoint (no restart).
    /// Already at that endpoint: the clock settles (not playing, finished).
    /// This is the retargetable form of play()/playReverse() and the primitive
    /// a two-state control (switch, hover feedback) should use.
    void playToward(EUIAnimDirection direction);
    /// Jump to a normalized position without animating (initial state, or a
    /// non-animated set): pauses and clears the finished flag.
    void setLerp(float value);

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
/// Two authoring forms share one evaluation path:
///   * two endpoints (`from` -> `to` with `ease`) - the common case;
///   * a keyframe curve (`keyframes`), which is the same track sampled at N
///     keys - used for multi-step motion such as slide in -> hold -> fade out.
/// A curve is still ONE property on ONE clock on ONE widget; multi-object
/// timelines with events stay in the Game UI clip player layer.
struct FUIAnimTrack
{
    std::string  id;   ///< animatable property id (FUIAnimPropertyDesc::id)
    FUIAnimValue from{};
    FUIAnimValue to{};
    EUIAnimEase  ease = EUIAnimEase::Linear;
    /// Non-empty switches evaluation to the piecewise curve (from/to unused).
    std::vector<FUIAnimKey> keyframes;
};

/// Framework-layer tween behaviour: one clock driving N animatable properties
/// of the behaviour's OWNER widget. Multi-widget orchestration (a track bound
/// to a widget by name, keyframe events, clip sequences) belongs to the Game
/// UI layer on top, not here.
///
/// Usage:
///
///   auto tween = ya::ui::animate(card, 0.25f);   // attaches to `card`
///   tween->fade(0.0f, 1.0f, ya::EUIAnimEase::OutCubic)
///        ->scale({0.9f, 0.9f}, {1.0f, 1.0f}, ya::EUIAnimEase::OutBack)
///        ->play();
///
///   // A widget's own animatable property (declared next to its table):
///   tween->track(ya::kAnimGauge, 0.0f, 1.0f);
///
/// Authoring calls return *this so tracks, duration and playback read as one
/// chain; the plain add*Track() names stay available for table-driven code.
class YA_GUI_API UITweenBehavior : public UIBehavior
{
  public:
    // === Authoring ===
    UITweenBehavior& addFloatTrack(std::string id, float from, float to, EUIAnimEase ease = EUIAnimEase::Linear);
    UITweenBehavior& addVec2Track(std::string id, glm::vec2 from, glm::vec2 to, EUIAnimEase ease = EUIAnimEase::Linear);
    UITweenBehavior& addVec4Track(std::string id, glm::vec4 from, glm::vec4 to, EUIAnimEase ease = EUIAnimEase::Linear);
    /// Typed variant: the value domain comes from the handle, so a wrong value
    /// type is a compile error instead of a runtime rejection.
    template <typename T>
    UITweenBehavior& track(const TUIAnimProperty<T>& property, T from, T to, EUIAnimEase ease = EUIAnimEase::Linear)
    {
        if constexpr (std::is_same_v<T, float>) {
            return addFloatTrack(std::string(property.id), from, to, ease);
        }
        else if constexpr (std::is_same_v<T, glm::vec2>) {
            return addVec2Track(std::string(property.id), from, to, ease);
        }
        else {
            static_assert(std::is_same_v<T, glm::vec4>,
                          "animatable properties support float / vec2 / vec4 value domains only");
            return addVec4Track(std::string(property.id), from, to, ease);
        }
    }
    /// The base widget render transform, spelled out for the common cases.
    UITweenBehavior& fade(float from, float to, EUIAnimEase ease = EUIAnimEase::Linear)
    {
        return addFloatTrack(std::string(kAnimOpacity.id), from, to, ease);
    }
    UITweenBehavior& scale(glm::vec2 from, glm::vec2 to, EUIAnimEase ease = EUIAnimEase::Linear)
    {
        return addVec2Track(std::string(kAnimRenderScale.id), from, to, ease);
    }
    UITweenBehavior& slide(glm::vec2 from, glm::vec2 to, EUIAnimEase ease = EUIAnimEase::Linear)
    {
        return addVec2Track(std::string(kAnimRenderTranslation.id), from, to, ease);
    }
    UITweenBehavior& tint(glm::vec4 from, glm::vec4 to, EUIAnimEase ease = EUIAnimEase::Linear)
    {
        return addVec4Track(std::string(kAnimTint.id), from, to, ease);
    }
    /// Keyframe curve on one property (keys in normalized clock time; see
    /// FUIAnimKey). Any number of keys, including a hold between two of them:
    ///
    ///   tween->curve(ya::ui::anim::translation, {ya::animKey(0.0f, glm::vec2(0.0f, 24.0f)),
    ///                                            ya::animKey(0.3f, glm::vec2(0.0f, 0.0f), OutCubic),
    ///                                            ya::animKey(0.7f, glm::vec2(0.0f, 0.0f)),
    ///                                            ya::animKey(1.0f, glm::vec2(0.0f, -12.0f), InCubic)});
    UITweenBehavior& addCurveTrack(std::string id, EUIAnimValueType type, std::vector<FUIAnimKey> keys);
    template <typename T>
    UITweenBehavior& curve(const TUIAnimProperty<T>& property, std::vector<FUIAnimKey> keys)
    {
        static_assert(requires { TUIAnimValueDomain<T>::value; },
                      "animatable properties support float / vec2 / vec4 value domains only");
        return addCurveTrack(std::string(property.id), animValueTypeOf<T>(), std::move(keys));
    }
    /// Tracks are resolved against the owner's animatable property table when
    /// the tween runs; a track whose id the owner does not expose is skipped
    /// (warned once per id). A tween never silently animates nothing.
    [[nodiscard]] size_t getTrackCount() const { return _tracks.size(); }
    [[nodiscard]] const FUIAnimTrack& getTrack(size_t index) const { return _tracks[index]; }
    void clearTracks();

    UITweenBehavior& setDuration(float seconds)
    {
        _clock.setDuration(seconds);
        return *this;
    }
    [[nodiscard]] float getDuration() const { return _clock.getDuration(); }
    UITweenBehavior& setLoop(bool bLoop)
    {
        _clock.setLoop(bLoop);
        return *this;
    }
    UITweenBehavior& setTimeScale(float timeScale)
    {
        _clock.setTimeScale(timeScale);
        return *this;
    }
    /// Called when a non-looping tween reaches its end (after the final value
    /// was applied). Use it to chain the next step.
    UITweenBehavior& setOnFinished(std::function<void()> callback)
    {
        _onFinished = std::move(callback);
        return *this;
    }

    // === Playback ===
    /// Apply t=0 and run forward. Replaying a finished tween restarts it.
    UITweenBehavior& play();
    /// Apply t=1 and run backward.
    UITweenBehavior& playReverse();
    /// Continue from the current position toward an endpoint (see
    /// UIAnimClock::playToward). The current value is applied immediately, so
    /// the first frame after the call is already consistent.
    UITweenBehavior& playToward(EUIAnimDirection direction);
    /// Jump to a normalized position and apply it once, without animating.
    UITweenBehavior& setLerpNow(float lerp);
    UITweenBehavior& stop();
    [[nodiscard]] bool  isPlaying() const { return _clock.isPlaying(); }
    [[nodiscard]] float getLerp() const { return _clock.getLerp(); }

    // === UIBehavior ===
    [[nodiscard]] bool wantsTick() const override;
    void tick(UIElement& owner, float deltaSeconds) override;
    void onDetached(UIElement& owner) override;

  private:
    void applyTracks(UIElement& owner, float lerp);
    /// Fire the end callback exactly once per run (play()/playReverse()/
    /// playToward() reset the latch).
    void settleFinishedCallback();
    /// Resolve each track's descriptor once (the owner's table is static, so
    /// a resolved descriptor stays valid). Cleared when the track list, the
    /// owner, or the widget identity changes.
    void resolveTrackDescriptors(UIElement& owner);
    /// Report a track that cannot run on this owner, once per track id.
    void warnTrackSkipped(UIElement& owner, const FUIAnimTrack& track, const char* reason);

    std::vector<FUIAnimTrack>  _tracks;
    /// Parallel to _tracks; lazily filled by resolveTrackDescriptors.
    std::vector<const FUIAnimPropertyDesc*> _resolved;
    UIAnimClock                _clock;
    std::function<void()>      _onFinished;
    /// Ids already warned about in this tween run (cleared on play*()).
    std::vector<std::string>   _warnedIds;
    bool                       _bFinishedFired = false;
};

// === Authoring entry points =================================================

/// Attach a tween to `widget` and return it for authoring / playback. The
/// widget owns the behaviour from here on, so dropping the returned handle is
/// safe: `ya::ui::animate(card, 0.2f)->fade(0.0f, 1.0f)->play();` is a complete
/// fire-and-forget animation.
[[nodiscard]] YA_GUI_API std::shared_ptr<UITweenBehavior> animate(UIElement& widget,
                                                                 float      duration = 0.25f);

/// Same, for a declarative builder before `.release()`/`.share()`, so a widget
/// and its animation are authored in one place:
///   auto card = ya::ui::border("Card");
///   ya::ui::animate(card, 0.25f)->fade(0.0f, 1.0f, ya::EUIAnimEase::OutCubic)->play();
template <typename TBuilder>
    requires requires(TBuilder& builder) { builder.widget(); }
[[nodiscard]] std::shared_ptr<UITweenBehavior> animate(TBuilder& builder, float duration = 0.25f)
{
    return animate(builder.widget(), duration);
}

} // namespace ya
