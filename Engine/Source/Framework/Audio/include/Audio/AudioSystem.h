#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ya
{

/// Named output group with its own volume. Master scales everything; the three
/// game buses are separate so a script can duck music under an effect.
enum class EAudioBus : uint8_t
{
    Master = 0,
    Music,
    Sfx,
    Ui,
    Count,
};

[[nodiscard]] const char* toString(EAudioBus bus);

/// A decoded, device-ready clip. Opaque: the backend owns the PCM and the
/// device format. Clips are shared by handle (`AudioClipRef`) and live in the
/// AudioSystem's catalog.
struct AudioClip
{
    virtual ~AudioClip() = default;
};

/// One playing sound. Opaque to callers: they hold it to stop it or read its
/// state. The backend owns the voices.
struct AudioVoice
{
    virtual ~AudioVoice() = default;
};

/// Everything `play` may override about a clip. Zero/negative fields mean
/// "use the default for that field".
struct AudioPlayDesc
{
    EAudioBus bus     = EAudioBus::Sfx;
    float     volume  = 1.0f;
    float     pitch   = 1.0f;   ///< Playback rate; 1 is the clip's own rate
    bool      bLoop   = false;
};

/**
 * @brief The engine's audio interface.
 *
 * One device, a clip catalog keyed by a stable id, and fire-and-forget voices.
 * A backend implements the device; the game and the script layer only see this
 * interface, so a self-mixed backend (showcase-roadmap A2) can replace SDL3
 * without them changing. There is no ECS or Scene dependency: a scene attaches
 * meaning to a voice, this layer only plays sound.
 *
 * Every call is safe with no device: a headless run (no audio hardware, CI)
 * answers `isAvailable() == false` and the play calls become no-ops, so a script
 * that plays a sound does not have to guard.
 */
class YA_AUDIO_API AudioSystem
{
  public:
    /// The process audio system. Created on first use; `init` opens the device.
    static AudioSystem& get();

    virtual ~AudioSystem() = default;

    /// Open the device. Idempotent. Returns false when no device can be opened
    /// (no hardware, headless); every later call is then a no-op. `bDebugDump`
    /// is unused by the SDL3 backend, kept for a future WAV writer.
    virtual bool init() = 0;
    [[nodiscard]] virtual bool isAvailable() const = 0;
    virtual void               shutdown() = 0;

    /// Load (and cache) a clip from `path`. An empty path or a failed decode
    /// returns null. The same path twice returns the same clip.
    [[nodiscard]] virtual std::shared_ptr<AudioClip> loadClip(const std::string& path) = 0;
    /// The already-loaded clip for `path`, or null. Does not touch the disk.
    [[nodiscard]] virtual std::shared_ptr<AudioClip> findClip(const std::string& path) const = 0;

    /// Start a clip. Returns a voice to stop it, or null when the clip is null
    /// or there is no device. A one-shot voice frees itself when it finishes.
    [[nodiscard]] virtual std::shared_ptr<AudioVoice> play(const std::shared_ptr<AudioClip>& clip,
                                                           const AudioPlayDesc&              desc = {}) = 0;
    /// Stop a voice now (with a short fade to avoid a click).
    virtual void stop(const std::shared_ptr<AudioVoice>& voice) = 0;

    /// Bus volume in [0, 1]; also scales every voice already playing on it.
    virtual void  setBusVolume(EAudioBus bus, float volume) = 0;
    [[nodiscard]] virtual float busVolume(EAudioBus bus) const = 0;

    /// Call once per frame. Advances voices, reclaims finished ones, and
    /// crossfades music. `dt` seconds.
    virtual void update(float dt) = 0;

    /// Stop every voice and cut the music (leaving Play / switching scenes).
    virtual void stopAll() = 0;

    /// Fade the current music out and a new clip in over `seconds`. A null clip
    /// just fades the music out. Uses Music bus volume as the target level.
    virtual void playMusic(const std::shared_ptr<AudioClip>& clip, float seconds = 0.0f) = 0;
    /// Fade the music out (and stop it) over `seconds`.
    virtual void stopMusic(float seconds = 0.0f) = 0;
};

} // namespace ya
