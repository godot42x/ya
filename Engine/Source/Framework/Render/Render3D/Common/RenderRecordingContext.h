#pragma once

#include "RHI/RenderDefines.h"

#include <cstdint>
#include <limits>

namespace ya
{

struct ICommandBuffer;
struct IRenderSurfaceContext;
struct RenderFrameData;
struct SceneViewportTask;

/// One GPU submission: one command buffer, one frame token, one host surface.
/// Persistent device/pipeline objects do not belong here. View-local
/// descriptors, upload slices and outputs do not belong here either.
struct RenderSubmissionContext
{
    uint64_t               frameToken  = 0;
    uint32_t               flightIndex = 0;
    ICommandBuffer*        cmdBuf      = nullptr;
    IRenderSurfaceContext* hostSurface = nullptr;

    [[nodiscard]] bool valid() const
    {
        return cmdBuf != nullptr && flightIndex < MAX_FLIGHTS_IN_FLIGHT;
    }
};

/// One View being recorded inside a submission. The resource set assigns
/// `viewSlot` when beginView succeeds; callers must not treat flightIndex as
/// a View identity.
struct RenderViewRecordingContext
{
    static constexpr uint32_t kInvalidViewSlot = std::numeric_limits<uint32_t>::max();

    const SceneViewportTask* task      = nullptr;
    const RenderFrameData*   frameData = nullptr;
    uint32_t                 viewSlot  = kInvalidViewSlot;
    Extent2D                 viewportExtent{};

    [[nodiscard]] bool valid() const { return frameData != nullptr; }
};

} // namespace ya
