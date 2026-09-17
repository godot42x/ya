#pragma once

#include "RHI/RenderDefines.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ya
{

/// Per-flight table of View-owned binding slots.
///
/// `MAX_FLIGHTS_IN_FLIGHT` is the GPU fence axis. View slots are a different
/// axis: one submission may record many Views into the same flight's command
/// buffer. Acquiring View N+1 must not replace, move, or rewrite View N.
///
/// A new frame token on a fence-safe flight rewinds the live View count so
/// slots can be reused. The same token is idempotent and keeps already
/// committed Views.
template <typename Binding>
class RenderViewBindingTable
{
    struct Flight
    {
        uint64_t             token         = 0;
        bool                 hasToken      = false;
        uint32_t             liveViewCount = 0;
        std::vector<Binding> views;
    };

    std::array<Flight, MAX_FLIGHTS_IN_FLIGHT> _flights{};

  public:
    bool beginSubmission(uint32_t flightIndex, uint64_t frameToken)
    {
        if (flightIndex >= _flights.size()) {
            return false;
        }

        Flight& flight = _flights[flightIndex];
        if (flight.hasToken && flight.token == frameToken) {
            return true;
        }

        flight.token         = frameToken;
        flight.hasToken      = true;
        flight.liveViewCount = 0;
        return true;
    }

    /// Next uncommitted slot for this flight. Grows storage if needed.
    /// Does not increment `liveViewCount` until commitNextView().
    Binding* mutableNextView(uint32_t flightIndex)
    {
        if (flightIndex >= _flights.size()) {
            return nullptr;
        }

        Flight& flight = _flights[flightIndex];
        if (!flight.hasToken) {
            return nullptr;
        }
        if (flight.liveViewCount > flight.views.size()) {
            return nullptr;
        }
        if (flight.liveViewCount == flight.views.size()) {
            flight.views.emplace_back();
        }
        return &flight.views[flight.liveViewCount];
    }

    bool commitNextView(uint32_t flightIndex)
    {
        if (flightIndex >= _flights.size()) {
            return false;
        }

        Flight& flight = _flights[flightIndex];
        if (!flight.hasToken || flight.liveViewCount >= flight.views.size()) {
            return false;
        }
        ++flight.liveViewCount;
        return true;
    }

    [[nodiscard]] const Binding* getView(uint32_t flightIndex, uint32_t viewSlot) const
    {
        if (flightIndex >= _flights.size()) {
            return nullptr;
        }
        const Flight& flight = _flights[flightIndex];
        if (viewSlot >= flight.liveViewCount) {
            return nullptr;
        }
        return &flight.views[viewSlot];
    }

    [[nodiscard]] Binding* getView(uint32_t flightIndex, uint32_t viewSlot)
    {
        return const_cast<Binding*>(
            static_cast<const RenderViewBindingTable*>(this)->getView(flightIndex, viewSlot));
    }

    [[nodiscard]] uint32_t liveViewCount(uint32_t flightIndex) const
    {
        if (flightIndex >= _flights.size()) {
            return 0;
        }
        return _flights[flightIndex].liveViewCount;
    }

    [[nodiscard]] uint32_t slotCapacity(uint32_t flightIndex) const
    {
        if (flightIndex >= _flights.size()) {
            return 0;
        }
        return static_cast<uint32_t>(_flights[flightIndex].views.size());
    }

    void clear() { _flights = {}; }
};

} // namespace ya
