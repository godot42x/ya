#pragma once

#include "Core/Common/RetainedResource.h"
#include "Render3D/Common/RenderRecordingContext.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ya
{

/// One GPU submission on a fence-safe flight.
///
/// Occupied from command-buffer begin until that flight is begun again with a
/// new token. Recording ending (`renderFrame` returning) does not drop
/// keepalives; the next fence-safe reuse of the flight does.
struct RenderSubmissionRecord
{
    uint64_t                          frameToken         = 0;
    bool                              occupied           = false;
    bool                              recordingComplete  = false;
    RenderSubmissionContext           context{};
    std::vector<RetainedResource>     keepalives;
};

/// Per-flight live submissions owned by RenderRuntime.
///
/// `MAX_FLIGHTS_IN_FLIGHT` is the GPU fence axis. Keepalives (graph-exported
/// images, overlay snapshot) must outlive `renderFrame()` and stay until the
/// same flight is reset after its fence. A second flight's begin must not drop
/// the first flight's owners.
class RenderSubmissionTable
{
    std::array<RenderSubmissionRecord, MAX_FLIGHTS_IN_FLIGHT> _flights{};

  public:
    bool begin(uint32_t flightIndex, uint64_t frameToken, const RenderSubmissionContext& context)
    {
        if (flightIndex >= _flights.size() || context.flightIndex != flightIndex || !context.valid()) {
            return false;
        }

        RenderSubmissionRecord& flight = _flights[flightIndex];
        if (flight.occupied && flight.frameToken == frameToken) {
            flight.context = context;
            return true;
        }

        flight.keepalives.clear();
        flight.frameToken        = frameToken;
        flight.occupied          = true;
        flight.recordingComplete = false;
        flight.context           = context;
        return true;
    }

    bool retain(uint32_t flightIndex, RetainedResource resource)
    {
        if (flightIndex >= _flights.size() || !resource) {
            return false;
        }

        RenderSubmissionRecord& flight = _flights[flightIndex];
        if (!flight.occupied) {
            return false;
        }
        flight.keepalives.push_back(std::move(resource));
        return true;
    }

    bool markRecordingComplete(uint32_t flightIndex)
    {
        if (flightIndex >= _flights.size()) {
            return false;
        }

        RenderSubmissionRecord& flight = _flights[flightIndex];
        if (!flight.occupied) {
            return false;
        }
        flight.recordingComplete = true;
        return true;
    }

    [[nodiscard]] const RenderSubmissionRecord* get(uint32_t flightIndex) const
    {
        if (flightIndex >= _flights.size()) {
            return nullptr;
        }
        const RenderSubmissionRecord& flight = _flights[flightIndex];
        return flight.occupied ? &flight : nullptr;
    }

    [[nodiscard]] const RenderSubmissionContext* context(uint32_t flightIndex) const
    {
        const RenderSubmissionRecord* record = get(flightIndex);
        return record ? &record->context : nullptr;
    }

    void clear() { _flights = {}; }
};

} // namespace ya
