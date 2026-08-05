#pragma once

#include "ForwardFrameGraphResources.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace ya
{

struct ForwardViewportStage;
struct ForwardViewportResources;
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
    struct TopologyDescription
    {
        std::vector<std::string_view>                     passOrder{};
        std::vector<std::pair<std::string_view, std::string_view>> dependencies{};
    };

    struct TopologyInputs
    {
        bool bHasShadowSubgraph  = false;
        bool bHasBloomSubgraph   = false;
        bool bHasPostprocessPass = true;
    };

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
        const ForwardViewportResources*                  viewportResources   = nullptr;
        std::vector<ForwardDirectionGizmoInput>          directionGizmos     = {};
        FrameContext*                                    postContext         = nullptr;
        bool                                             bEnableShadow       = false;
        bool                                             bPostprocessOutputIsSRGB = false;
        std::function<void(ICommandBuffer*, Extent2D)>   recordViewportOverlays{};
    };

    [[nodiscard]] static TopologyDescription describeTopology(const TopologyInputs& inputs);
    void build(const BuildDependencies& deps, const BuildInputs& inputs) const;
};

} // namespace ya
