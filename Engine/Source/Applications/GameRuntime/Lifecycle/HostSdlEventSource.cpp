#include "HostSdlEventSource.h"

#include "GameRuntime/Utility/SDLMisc.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include <SDL3/SDL.h>

namespace ya
{

void HostSdlEventSource::pollEvents(const std::function<void(const Event&)>& emit)
{
    SDL_Event event;
    YA_PROFILE_SCOPE("Frame/EventPump");
    YA_PERF_SCOPE(perf::sample::frameEventPump(), perf::metric::cpuTimeMs(), perf::domain::game());
    while (SDL_PollEvent(&event)) {
        processSDLEvent(
            event,
            [&emit](const auto& translated) {
                emit(translated);
            });
    }
}

} // namespace ya
