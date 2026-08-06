#pragma once

#include "ForwardFrameGraphResources.h"
#include "Runtime/Rendering/Forward/ForwardFrameResourceSet.h"
#include "Runtime/Rendering/Forward/ForwardViewportStage.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"

#include <memory>
#include <vector>

namespace ya
{

struct ShadowStage;
struct PostProcessingStage;
struct RenderTargetCreateInfo;

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
        ShadowStage*          shadowStage      = nullptr;
        PostProcessingStage*  postProcessStage = nullptr;
    };

    struct BuildInputs
    {
        RenderGraph*                                     graph               = nullptr;
        RenderStageContext*                              stageCtx            = nullptr;
        ForwardFrameResourceSet::Binding                 frameBinding        = {};
        const RenderTargetCreateInfo*                    viewportRTSpec      = nullptr;
        std::vector<ForwardDirectionGizmoInput>          directionGizmos     = {};
        ForwardViewportStage::PassContext*              viewportPassContext = nullptr;
        FrameContext*                                    postContext         = nullptr;
        bool                                             bEnableShadow       = false;
        bool                                             bPostprocessOutputIsSRGB = false;
        std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    };

    void build(const BuildDependencies& deps, const BuildInputs& inputs) const;
};

} // namespace ya
