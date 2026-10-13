#include "Audio/AudioSystem.h"
#include "Audio/SdlAudioBackend.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>

#include <mutex>
#include <vector>

namespace ya
{

AudioSystem& AudioSystem::get()
{
    static SdlAudioSystem instance;
    return instance;
}

const char* toString(EAudioBus bus)
{
    switch (bus) {
    case EAudioBus::Master:
        return "master";
    case EAudioBus::Music:
        return "music";
    case EAudioBus::Sfx:
        return "sfx";
    case EAudioBus::Ui:
        return "ui";
    case EAudioBus::Count:
        break;
    }
    return "?";
}

} // namespace ya
