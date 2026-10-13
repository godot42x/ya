#pragma once

#include "Audio/AudioSystem.h"

namespace ya
{

/// The SDL3-backed audio system. `AudioSystem::get()` returns one of these.
/// Declared here so the definition lives in one translation unit while the
/// interface stays in AudioSystem.h.
struct SdlAudioSystem final : public AudioSystem
{
    SdlAudioSystem();
    ~SdlAudioSystem() override;

    bool init() override;
    [[nodiscard]] bool isAvailable() const override;
    void shutdown() override;

    [[nodiscard]] std::shared_ptr<AudioClip> loadClip(const std::string& path) override;
    [[nodiscard]] std::shared_ptr<AudioClip> findClip(const std::string& path) const override;

    [[nodiscard]] std::shared_ptr<AudioVoice> play(const std::shared_ptr<AudioClip>& clip,
                                                   const AudioPlayDesc&              desc) override;
    void stop(const std::shared_ptr<AudioVoice>& voice) override;

    void  setBusVolume(EAudioBus bus, float volume) override;
    [[nodiscard]] float busVolume(EAudioBus bus) const override;

    void update(float dt) override;
    void stopAll() override;

    void playMusic(const std::shared_ptr<AudioClip>& clip, float seconds) override;
    void stopMusic(float seconds) override;

    struct Impl;

  private:
    std::unique_ptr<Impl> _impl;
};

} // namespace ya
