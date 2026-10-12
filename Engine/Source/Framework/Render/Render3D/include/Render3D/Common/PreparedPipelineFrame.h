#pragma once

#include "Core/Base.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Deferred/DeferredPipelineRecipe.h"

#include <cstdint>
#include <vector>

namespace ya
{

struct RenderFramePlan;

/// Result of preparing one frame's pipeline composition before command
/// recording, at the safe point.
///
/// It holds what can be resolved without a live submission: the resolved
/// composition per View (feature enablement, resource requirements, per-View
/// values). Bindings that need the submission (descriptors, uploads) are
/// allocated per View during recording from this same resolved data.
///
/// Lifetime: created in the safe point, consumed by the family recording of the
/// same frame, destroyed after submit.
struct PreparedPipelineFrame
{
    ERenderPipelineKind pipelineKind = ERenderPipelineKind::Forward;
    uint64_t            hostTick     = 0;
    uint32_t            flightIndex  = 0;

    /// Typed Deferred pipeline composition.
    struct DeferredComposition
    {
        std::vector<DeferredPipelineRecipe::ResolvedView> resolved;
    };
    DeferredComposition deferred;
};

/// Result type for frame preparation (can fail if resources unavailable).
using PreparedPipelineFrameResult = std::optional<PreparedPipelineFrame>;

} // namespace ya
