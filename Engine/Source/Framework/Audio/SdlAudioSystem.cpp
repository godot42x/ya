#include "Audio/SdlAudioBackend.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>

// stb_vorbis is single-header with the implementation in the .c body; this is
// the one translation unit that instantiates it. .ogg is the format the game
// ships (Kenney CC0 packs).
#include <stb/stb_vorbis.c>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ya
{

namespace
{

constexpr int kFreq     = 48000;
constexpr int kChannels = 2;

/// A decoded clip: interleaved float PCM at `freq`, `channels`.
struct SdlClip final : public AudioClip
{
    std::vector<float> samples;   // interleaved
    int                freq     = kFreq;
    int                channels = kChannels;

    [[nodiscard]] double seconds() const
    {
        const int frames = channels > 0 ? static_cast<int>(samples.size()) / channels : 0;
        return freq > 0 ? static_cast<double>(frames) / freq : 0.0;
    }
};

/// A playing voice: its own stream, fed a chunk at a time by update(). One
/// stream per voice keeps mixing trivial (SDL mixes the bound streams) at the
/// cost of a stream per concurrent sound, which is fine for a small game.
struct SdlVoice final : public AudioVoice
{
    SDL_AudioStream*   stream  = nullptr;
    std::shared_ptr<AudioClip> clip;
    size_t             cursor  = 0;        // float samples consumed
    EAudioBus          bus     = EAudioBus::Sfx;
    float              volume  = 1.0f;
    float              pitch   = 1.0f;
    bool               bLoop   = false;
    bool               bDone   = false;
    // Music fades: `gain` ramps from `gainStart` to `gainTarget` over `fade`.
    float              gain       = 1.0f;
    float              gainStart  = 1.0f;
    float              gainTarget = 1.0f;
    float              fade       = 0.0f;   // seconds; 0 = no fade in progress
    float              fadeT      = 0.0f;
    bool               bStopWhenFaded = false;
};

/// Feed `voice` up to `bytes` of clip data into its stream, resampling for
/// pitch by asking SDL to convert from a shifted source rate. Loop wraps the
/// cursor; a one-shot that has produced all its samples stops putting data and
/// is reclaimed once SDL has drained the queue.
void feedVoice(SdlVoice& voice)
{
    auto* clip = static_cast<const SdlClip*>(voice.clip.get());
    if (clip == nullptr || clip->samples.empty() || voice.stream == nullptr) {
        voice.bDone = true;
        return;
    }
    // Keep about a tenth of a second queued; enough to ride out a frame hitch
    // without buffering the whole clip up front.
    const int targetBytes = static_cast<int>(static_cast<size_t>(clip->freq) * clip->channels * sizeof(float) / 8);
    const int queued      = SDL_GetAudioStreamQueued(voice.stream);
    if (queued >= targetBytes) {
        return;
    }

    const size_t totalSamples = clip->samples.size();
    size_t       want         = static_cast<size_t>(std::max(targetBytes - queued, 0)) / sizeof(float);
    want                      = std::max<size_t>(want, 1024);
    std::vector<float> chunk;
    chunk.reserve(want);

    while (chunk.size() < want) {
        if (voice.cursor >= totalSamples) {
            if (voice.bLoop) {
                voice.cursor = 0;
            } else {
                break;
            }
        }
        const size_t available = totalSamples - voice.cursor;
        const size_t take      = std::min(available, want - chunk.size());
        chunk.insert(chunk.end(), clip->samples.begin() + static_cast<std::ptrdiff_t>(voice.cursor),
                     clip->samples.begin() + static_cast<std::ptrdiff_t>(voice.cursor + take));
        voice.cursor += take;
    }
    if (chunk.empty()) {
        return;  // one-shot finished producing; SDL drains the rest
    }
    SDL_PutAudioStreamData(voice.stream, chunk.data(), static_cast<int>(chunk.size() * sizeof(float)));
}

} // namespace

struct SdlAudioSystem::Impl
{
    bool                                          bInit       = false;
    bool                                          bAvailable  = false;
    std::unordered_map<std::string, std::shared_ptr<AudioClip>> catalog;
    std::vector<std::shared_ptr<SdlVoice>>        voices;
    std::shared_ptr<SdlVoice>                     music;
    float                                         busVolume[static_cast<size_t>(EAudioBus::Count)] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool                                          bMusicFadeOut = false;
    float                                         musicFadeSeconds = 0.0f;
    float                                         musicFadeT       = 0.0f;
    std::shared_ptr<AudioClip>                    pendingMusic;
};

SdlAudioSystem::SdlAudioSystem() : _impl(std::make_unique<Impl>())
{
}

SdlAudioSystem::~SdlAudioSystem()
{
    stopAll();
    shutdown();
}

bool SdlAudioSystem::init()
{
    if (_impl->bInit) {
        return _impl->bAvailable;
    }
    _impl->bInit = true;
    // Video/input may already have initialized SDL; add the audio subsystem.
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            YA_CORE_WARN("audio: no device ({}); sounds are silent", SDL_GetError());
            _impl->bAvailable = false;
            return false;
        }
    }
    _impl->bAvailable = true;
    YA_CORE_INFO("audio: SDL3 device ready ({} Hz, {} ch)", kFreq, kChannels);
    return true;
}

bool SdlAudioSystem::isAvailable() const
{
    return _impl->bAvailable;
}

void SdlAudioSystem::shutdown()
{
    if (!_impl->bInit) {
        return;
    }
    for (auto& voice : _impl->voices) {
        if (voice->stream) {
            SDL_DestroyAudioStream(voice->stream);
        }
    }
    _impl->voices.clear();
    _impl->music.reset();
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    _impl->bAvailable = false;
    _impl->bInit      = false;
}

std::shared_ptr<AudioClip> SdlAudioSystem::loadClip(const std::string& path)
{
    if (path.empty()) {
        return nullptr;
    }
    if (auto it = _impl->catalog.find(path); it != _impl->catalog.end()) {
        return it->second;
    }
    auto clip = std::make_shared<SdlClip>();
    int  channels = 0;
    int  freq     = 0;
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &freq, &decoded);
    if (frames <= 0 || decoded == nullptr) {
        YA_CORE_WARN("audio: failed to decode '{}'", path);
        return nullptr;
    }
    clip->channels = channels > 0 ? channels : kChannels;
    clip->freq     = freq > 0 ? freq : kFreq;
    clip->samples.assign(static_cast<size_t>(frames) * static_cast<size_t>(clip->channels), 0.0f);
    for (size_t i = 0; i < clip->samples.size(); ++i) {
        clip->samples[i] = static_cast<float>(decoded[i]) / 32768.0f;
    }
    free(decoded);  // stb_vorbis_decoded_filename allocates with malloc

    _impl->catalog[path] = clip;
    YA_CORE_INFO("audio: loaded '{}' ({:.2f}s, {} ch @ {} Hz)", path, clip->seconds(), clip->channels, clip->freq);
    return clip;
}

std::shared_ptr<AudioClip> SdlAudioSystem::findClip(const std::string& path) const
{
    if (auto it = _impl->catalog.find(path); it != _impl->catalog.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<AudioVoice> SdlAudioSystem::play(const std::shared_ptr<AudioClip>& clip, const AudioPlayDesc& desc)
{
    if (!_impl->bAvailable || !clip) {
        return nullptr;
    }
    auto* sdlClip = static_cast<const SdlClip*>(clip.get());
    if (sdlClip->samples.empty()) {
        return nullptr;
    }
    SDL_AudioSpec src{SDL_AUDIO_F32, sdlClip->channels, static_cast<int>(static_cast<double>(sdlClip->freq) * std::max(desc.pitch, 0.01f))};
    SDL_AudioSpec dst{SDL_AUDIO_F32, kChannels, kFreq};
    auto*         stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dst, nullptr, nullptr);
    if (stream == nullptr) {
        YA_CORE_WARN("audio: open stream failed: {}", SDL_GetError());
        return nullptr;
    }
    SDL_SetAudioStreamFormat(stream, &src, &dst);

    auto voice      = std::make_shared<SdlVoice>();
    voice->stream   = stream;
    voice->clip     = clip;
    voice->bus      = desc.bus;
    voice->volume   = desc.volume;
    voice->pitch    = desc.pitch;
    voice->bLoop    = desc.bLoop;
    voice->gain     = 1.0f;
    voice->gainTarget = 1.0f;
    const float busGain = _impl->busVolume[static_cast<size_t>(EAudioBus::Master)]
                        * _impl->busVolume[static_cast<size_t>(desc.bus)];
    SDL_SetAudioStreamGain(stream, voice->volume * busGain);
    SDL_ResumeAudioStreamDevice(stream);
    feedVoice(*voice);
    _impl->voices.push_back(voice);
    return voice;
}

void SdlAudioSystem::stop(const std::shared_ptr<AudioVoice>& voice)
{
    if (!voice) {
        return;
    }
    auto* sdlVoice = static_cast<SdlVoice*>(voice.get());
    sdlVoice->bDone = true;
    if (sdlVoice->stream) {
        SDL_ClearAudioStream(sdlVoice->stream);
    }
}

void SdlAudioSystem::setBusVolume(EAudioBus bus, float volume)
{
    if (bus == EAudioBus::Count) {
        return;
    }
    _impl->busVolume[static_cast<size_t>(bus)] = std::clamp(volume, 0.0f, 1.0f);
    // Re-apply to every live voice so a change is heard immediately.
    const float master = _impl->busVolume[static_cast<size_t>(EAudioBus::Master)];
    for (auto& voice : _impl->voices) {
        if (voice->stream) {
            const float busGain = master * _impl->busVolume[static_cast<size_t>(voice->bus)];
            SDL_SetAudioStreamGain(voice->stream, voice->volume * busGain * voice->gain);
        }
    }
}

float SdlAudioSystem::busVolume(EAudioBus bus) const
{
    if (bus == EAudioBus::Count) {
        return 1.0f;
    }
    return _impl->busVolume[static_cast<size_t>(bus)];
}

void SdlAudioSystem::update(float dt)
{
    if (!_impl->bAvailable) {
        return;
    }
    // Music crossfade bookkeeping.
    if (_impl->bMusicFadeOut) {
        _impl->musicFadeT += dt;
        const float k = _impl->musicFadeSeconds > 0.0f ? _impl->musicFadeT / _impl->musicFadeSeconds : 1.0f;
        if (_impl->music && _impl->music->stream) {
            _impl->music->gain = std::max(0.0f, 1.0f - k);
            const float master  = _impl->busVolume[static_cast<size_t>(EAudioBus::Master)];
            const float busGain = master * _impl->busVolume[static_cast<size_t>(EAudioBus::Music)];
            SDL_SetAudioStreamGain(_impl->music->stream, _impl->music->volume * busGain * _impl->music->gain);
        }
        if (k >= 1.0f) {
            _impl->bMusicFadeOut = false;
            if (_impl->music) {
                stop(_impl->music);
            }
            _impl->music.reset();
            if (_impl->pendingMusic) {
                AudioPlayDesc desc;
                desc.bus  = EAudioBus::Music;
                desc.bLoop = true;
                auto voice = play(_impl->pendingMusic, desc);
                if (auto* sdl = dynamic_cast<SdlVoice*>(voice.get())) {
                    sdl->fade       = _impl->musicFadeSeconds;
                    sdl->fadeT      = 0.0f;
                    sdl->gainStart  = 0.0f;
                    sdl->gainTarget = 1.0f;
                    sdl->gain       = 0.0f;
                }
                _impl->music = std::static_pointer_cast<SdlVoice>(voice);
                _impl->pendingMusic.reset();
            }
        }
    } else if (_impl->music && _impl->music->fade > 0.0f) {
        _impl->music->fadeT += dt;
        const float k = std::min(1.0f, _impl->music->fadeT / _impl->music->fade);
        _impl->music->gain = _impl->music->gainStart + (_impl->music->gainTarget - _impl->music->gainStart) * k;
        const float master  = _impl->busVolume[static_cast<size_t>(EAudioBus::Master)];
        const float busGain = master * _impl->busVolume[static_cast<size_t>(EAudioBus::Music)];
        SDL_SetAudioStreamGain(_impl->music->stream, _impl->music->volume * busGain * _impl->music->gain);
        if (k >= 1.0f) {
            _impl->music->fade = 0.0f;
        }
    }

    // Advance voices, drop finished ones.
    bool needsFeed = false;
    for (auto& voice : _impl->voices) {
        if (voice->bDone) {
            continue;
        }
        feedVoice(*voice);
        needsFeed = true;
        // A one-shot is finished once it has produced all its samples and SDL
        // has drained the queue.
        auto* clip = static_cast<const SdlClip*>(voice->clip.get());
        const bool produced = voice->cursor >= clip->samples.size();
        if (!voice->bLoop && produced && SDL_GetAudioStreamQueued(voice->stream) == 0) {
            voice->bDone = true;
        }
    }
    (void)needsFeed;
    std::erase_if(_impl->voices, [](const std::shared_ptr<SdlVoice>& voice) {
        if (!voice->bDone) {
            return false;
        }
        if (voice->stream) {
            SDL_DestroyAudioStream(voice->stream);
            voice->stream = nullptr;
        }
        return true;
    });
}

void SdlAudioSystem::stopAll()
{
    for (auto& voice : _impl->voices) {
        if (voice->stream) {
            SDL_ClearAudioStream(voice->stream);
        }
        voice->bDone = true;
    }
    _impl->music.reset();
    _impl->pendingMusic.reset();
    _impl->bMusicFadeOut = false;
}

void SdlAudioSystem::playMusic(const std::shared_ptr<AudioClip>& clip, float seconds)
{
    if (!_impl->bAvailable) {
        return;
    }
    if (seconds <= 0.0f) {
        // Cut: stop the old music now, start the new one at full gain.
        if (_impl->music) {
            stop(_impl->music);
            _impl->music.reset();
        }
        if (clip) {
            AudioPlayDesc desc;
            desc.bus   = EAudioBus::Music;
            desc.bLoop = true;
            _impl->music = std::static_pointer_cast<SdlVoice>(play(clip, desc));
        }
        return;
    }
    _impl->pendingMusic     = clip;
    _impl->musicFadeSeconds = seconds;
    _impl->musicFadeT       = 0.0f;
    _impl->bMusicFadeOut    = true;
    if (!_impl->music) {
        // Nothing playing: skip straight to the fade-in by advancing the fade.
        _impl->musicFadeT = seconds;
    }
}

void SdlAudioSystem::stopMusic(float seconds)
{
    if (seconds <= 0.0f) {
        if (_impl->music) {
            stop(_impl->music);
            _impl->music.reset();
        }
        _impl->pendingMusic.reset();
        _impl->bMusicFadeOut = false;
        return;
    }
    _impl->pendingMusic     = nullptr;
    _impl->musicFadeSeconds = seconds;
    _impl->musicFadeT       = 0.0f;
    _impl->bMusicFadeOut    = true;
}

} // namespace ya
