#pragma once

#include "RHI/Core/RenderTexture.h"
#include "RHI/RenderDefines.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ya
{
struct ViewTargetAllocation;

/// Offscreen identity of one View. Not an OS window, swapchain, or a global
/// viewport-state slot. Extent/format belong to this View; another View
/// in the same submission must not share or overwrite this record.
struct RenderViewOutputDesc
{
    uint64_t   viewId      = 0;
    Extent2D   extent{};
    EFormat::T colorFormat = EFormat::Undefined;
    EFormat::T depthFormat = EFormat::Undefined;

    [[nodiscard]] bool hasExtent() const { return extent.width > 0 && extent.height > 0; }
};

struct RenderViewOutput
{
    RenderViewOutputDesc           desc{};
    std::shared_ptr<RenderTexture> color;
    std::shared_ptr<RenderTexture> depth;
    std::shared_ptr<RenderTexture> display;
    std::shared_ptr<RenderTexture> entityId;
    std::shared_ptr<RenderTexture> bloomExtract;
    std::shared_ptr<RenderTexture> bloomBlur;
    std::shared_ptr<RenderTexture> bloomComposite;
    std::shared_ptr<RenderTexture> ssao;
    std::array<std::shared_ptr<RenderTexture>, 4> gBufferColors{};
    std::shared_ptr<ViewTargetAllocation> targets;
    uint64_t allocationGeneration = 0;

    [[nodiscard]] std::shared_ptr<RenderTexture> displayImage() const
    {
        return display ? display : color;
    }
};

/// Per-flight table of View-owned offscreen outputs.
///
/// `MAX_FLIGHTS_IN_FLIGHT` is the fence axis. Publishing View B must not
/// replace View A's handles, extents, or formats. A new token on a fence-safe
/// flight rewinds live count; the same token keeps already published Views.
class RenderViewOutputTable
{
    struct Flight
    {
        uint64_t                                   token         = 0;
        bool                                       hasToken      = false;
        uint32_t                                   liveViewCount = 0;
        std::vector<std::unique_ptr<RenderViewOutput>> views;
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

    const RenderViewOutput* publish(uint32_t flightIndex, RenderViewOutput output)
    {
        if (flightIndex >= _flights.size() || output.desc.viewId == 0) {
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
            flight.views.push_back(std::make_unique<RenderViewOutput>(std::move(output)));
        }
        else {
            *flight.views[flight.liveViewCount] = std::move(output);
        }
        const RenderViewOutput* published = flight.views[flight.liveViewCount].get();
        ++flight.liveViewCount;
        return published;
    }

    [[nodiscard]] const RenderViewOutput* get(uint32_t flightIndex, uint32_t viewSlot) const
    {
        if (flightIndex >= _flights.size()) {
            return nullptr;
        }
        const Flight& flight = _flights[flightIndex];
        if (viewSlot >= flight.liveViewCount) {
            return nullptr;
        }
        return flight.views[viewSlot].get();
    }

    [[nodiscard]] const RenderViewOutput* find(uint32_t flightIndex, uint64_t viewId) const
    {
        if (flightIndex >= _flights.size() || viewId == 0) {
            return nullptr;
        }
        const Flight& flight = _flights[flightIndex];
        for (uint32_t slot = 0; slot < flight.liveViewCount; ++slot) {
            if (flight.views[slot]->desc.viewId == viewId) {
                return flight.views[slot].get();
            }
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t liveViewCount(uint32_t flightIndex) const
    {
        if (flightIndex >= _flights.size()) {
            return 0;
        }
        return _flights[flightIndex].liveViewCount;
    }

    /// Remove one View's publication from every flight. Unregistering a View
    /// calls this, so a consumer can no longer observe the destroyed View;
    /// already recorded submissions keep their own keepalive copies instead.
    void dropView(uint64_t viewId)
    {
        if (viewId == 0) {
            return;
        }
        for (Flight& flight : _flights) {
            for (auto it = flight.views.begin(); it != flight.views.end();) {
                const size_t slot    = static_cast<size_t>(it - flight.views.begin());
                const bool   wasLive = slot < flight.liveViewCount;
                if ((*it)->desc.viewId == viewId) {
                    it = flight.views.erase(it);
                    if (wasLive) {
                        --flight.liveViewCount;
                    }
                    continue;
                }
                ++it;
            }
        }
    }

    void clear() { _flights = {}; }
};

} // namespace ya
