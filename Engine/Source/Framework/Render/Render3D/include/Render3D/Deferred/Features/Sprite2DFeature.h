#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"

#include <cstdint>

namespace ya
{

struct RenderFrameData;
struct Sprite2DStage;
class RenderSubmission;

/// Scene sprites (world-space quads) drawn into the lit color with the GBuffer
/// depth, before bloom. The feature owns its graph pass; instance upload and
/// texture tables are prepared here from the View's extracted sprite bucket.
struct Sprite2DFeature
{
    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D               viewExtent{};
        const RenderFrameData* frameData = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
    };

    struct PrepareContext
    {
        RenderSubmission*      submission = nullptr;
        IRender*               render     = nullptr;
        Sprite2DStage*         stage      = nullptr;
        const RenderFrameData* frameData  = nullptr;
        uint32_t               alignment  = 1;
    };

    struct Prepared
    {
        Sprite2DPassBindings bindings{};
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

    [[nodiscard]] GraphOutputs append(RenderGraph&               graph,
                                      Sprite2DStage*             stage,
                                      const FeatureGraphContext& ctx,
                                      const Facts&               facts,
                                      const Resolved&            resolved,
                                      const Prepared&            prepared,
                                      const Inputs&              inputs) const;
};

} // namespace ya
