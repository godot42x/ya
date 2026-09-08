#include "HostSdlEventSource.h"

#include "Core/Event.h"
#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Os/OsEvent.h"

namespace ya
{

void HostSdlEventSource::pollEvents(const std::function<void(const Event&)>& emit)
{
    YA_PROFILE_SCOPE("Frame/EventPump");
    YA_PERF_SCOPE(perf::sample::frameEventPump(), perf::metric::cpuTimeMs(), perf::domain::game());
    OsEventPump::poll([&emit](const Event& event) {
        switch (event.getEventType()) {
        case EEvent::WindowMinimize: {
            YA_CORE_INFO("Window minimized");
            break;
        }
        case EEvent::WindowRestore: {
            YA_CORE_INFO("Window restored/maximized");
            break;
        }
        default: {
            break;
        }
        }
        emit(event);
    });
}

} // namespace ya
