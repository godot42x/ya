#pragma once

#include "Graph/RenderGraph.h"
#include "RHI/Core/DescriptorSet.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

namespace ya
{

/// Skybox background feature: continues the lit color with the Scene's skybox,
/// reading the scene depth so geometry occludes it.
///
/// The feature owns its graph pass; the skybox frame UBO slice and its
/// descriptor set arrive through the View's prepared binding for now (the
/// payload moves into this feature's local preparation in the provider step).
struct SkyboxFeature
{
    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D                             viewExtent{};
        ViewOverlayStage::FrameInputs::SkyboxInput skybox{};
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
        RGBufferHandle  frame{};
        RGBufferRange   frameRange{};
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

    /// Skybox color continuation. The frame UBO handle/range arrive as inputs;
    /// the packet (cubemap set, mesh, frame descriptor set) comes from Facts.
    [[nodiscard]] GraphOutputs append(RenderGraph&                graph,
                                      ViewOverlayStage*           stage,
                                      const FeatureGraphContext&  ctx,
                                      const Facts&                facts,
                                      const Resolved&             resolved,
                                      const Inputs&               inputs) const;
};

} // namespace ya
