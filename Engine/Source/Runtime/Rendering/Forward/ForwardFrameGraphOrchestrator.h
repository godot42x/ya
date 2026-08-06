#pragma once

#include "ForwardFrameGraphPasses.h"
#include "Runtime/Rendering/Forward/ForwardFrameResourceSet.h"
#include "Runtime/Rendering/Forward/ForwardViewportStage.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"

#include <memory>
#include <vector>

namespace ya
{

struct ShadowStage;

/// Top-level Forward frame graph orchestration (FG-706).
///
/// Owns the graph build order so the pipeline no longer inlines the whole
/// viewport sequence. The pipeline stays responsible for the frame boundary
/// (resource upload), scene snapshots (direction gizmos), executor execution
/// and publishing exported outputs.
struct ForwardFrameGraphOrchestrator
{
    struct BuildDependencies
    {
        ForwardViewportStage* viewportStage    = nullptr;
        EntityIdViewportPass* entityIdPass     = nullptr;
        ShadowStage*          shadowStage      = nullptr;
        PostProcessingStage*  postProcessStage = nullptr;
    };

    using BuildInputs = forward_frame_graph::BuildInputs;

    void build(const BuildDependencies& deps, const BuildInputs& inputs) const;
};

} // namespace ya
