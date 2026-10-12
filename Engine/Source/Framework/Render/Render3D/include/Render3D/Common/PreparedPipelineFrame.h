#pragma once

#include "Core/Base.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Deferred/DeferredPipelineRecipe.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ya
{

struct RenderFramePlan;
struct SceneViewRecording;

/// Result of preparing one frame's pipeline composition before command recording.
/// 
/// Holds resolved resource requirements, prepared bindings, and target leases.
/// All recording contexts receive their data from this single prepared instance.
/// 
/// Lifetime: created in safe point, consumed during recording, destroyed after submit.
struct PreparedPipelineFrame
{
    ERenderPipelineKind pipelineKind = ERenderPipelineKind::Forward;
    uint64_t            hostTick     = 0;
    uint32_t            flightIndex  = 0;

    // Per-view prepared data (opaque storage for now, will be typed in full implementation)
    struct PreparedView
    {
        uint64_t viewId = 0;
        // Resolved composition, prepared bindings, target leases go here
        // C1: minimal placeholder; C2-C3 will expand to full typed composition
    };

    std::vector<PreparedView> views;

    // C2: Typed Deferred pipeline composition
    struct DeferredComposition
    {
        std::vector<DeferredPipelineRecipe::ResolvedView> resolved;
        std::vector<DeferredPipelineRecipe::PreparedView> prepared;
    };
    DeferredComposition deferred;

    [[nodiscard]] bool isEmpty() const { return views.empty(); }
};

/// Result type for frame preparation (can fail if resources unavailable).
using PreparedPipelineFrameResult = std::optional<PreparedPipelineFrame>;

} // namespace ya
