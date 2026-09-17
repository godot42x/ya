#pragma once

#include "RHI/RenderDefines.h"

#include <cstdint>
#include <limits>

namespace ya
{

struct ICommandBuffer;
struct RenderFrameData;
struct SceneViewportTask;

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
