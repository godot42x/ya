#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Composition/FeatureProtocol.h"

namespace ya
{

/// The Deferred pipeline's forward-opaque slot.
///
/// Forward-rendered opaque content continues the lit color and the scene depth
/// before the skybox. The slot is part of the Deferred map
/// ("ForwardOpaque attachment 操作，当前无 draw"), so its Load/Store contract is
/// kept even while no product content uses it; deleting the pass needs separate
/// resource-semantics evidence, not a cleanup commit.
struct ForwardOpaqueFeature
{
    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D viewExtent{};
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
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

    [[nodiscard]] GraphOutputs append(RenderGraph&               graph,
                                      const FeatureGraphContext& ctx,
                                      const Facts&               facts,
                                      const Resolved&            resolved,
                                      const Inputs&              inputs) const;
};

} // namespace ya
