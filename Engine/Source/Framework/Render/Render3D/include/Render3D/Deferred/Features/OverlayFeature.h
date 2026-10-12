#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

namespace ya
{

struct RenderFrameData;

/// Billboard/gizmo overlay feature: continues the post-color-effect stream with
/// camera-facing billboards, direction gizmos and the debug overlay, reading the
/// scene depth so authored geometry occludes them.
///
/// The overlay packet (billboards/gizmos) is per-View recording data; it is
/// assembled by the pipeline and handed in, not read from live ECS here.
struct OverlayFeature
{
    using Stage = ViewOverlayStage;

    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D                       viewExtent{};
        ViewOverlayStage::FrameInputs* inputs = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
    };

    struct PrepareContext
    {
        RenderSubmission*             submission = nullptr;
        IRender*                      render     = nullptr;
        ViewOverlayStage*             stage      = nullptr;
        ViewOverlayStage::FrameInputs* inputs    = nullptr;
        const RenderFrameData*        frameData  = nullptr;
        uint32_t                      alignment = 1;
    };

    struct Prepared
    {
        OverlayPassBindings bindings{};
    };

    struct Resolved
    {
        bool bEnabled = false;
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&                graph,
                                                            Stage*                      stage,
                                                            const FeatureGraphContext&  ctx,
                                                            const Facts&                facts,
                                                            const Resolved&             resolved,
                                                            const Prepared&             prepared,
                                                            const Inputs&               inputs) const;
};

static_assert(Feature<OverlayFeature>, "OverlayFeature must satisfy the composition Feature protocol");

} // namespace ya
