#pragma once

#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"

namespace ya
{

struct FrameContext;
struct PostProcessingStage;
class RenderSubmission;

/// Finalize: the mandatory display pass. It applies the configured grading (or
/// none) and the display encoding to the linear color stream and writes the
/// View's DisplayColor target, which is what publication exposes as `display`.
///
/// The pass runs every frame; grading/Bloom settings only change its look, never
/// whether it exists.
struct FinalizeFeature
{
    struct Settings
    {
        bool           bEnabled      = true;
        bool           bOutputIsSRGB = false;
        EFormat::T     displayFormat = EFormat::R8G8B8A8_UNORM;
        EImageUsage::T displayUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc;
    };

    struct Facts
    {
        Extent2D       viewExtent{};
        FrameContext*  postContext = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle color{};
    };

    struct PrepareContext
    {
        RenderSubmission*    submission = nullptr;
        PostProcessingStage* stage      = nullptr;
    };

    struct Prepared
    {
        ToneMapPassBindings toneMap{};
    };

    struct Resolved
    {
        bool                bEnabled      = false;
        bool                bOutputIsSRGB = false;
        ViewTargetRequirement target{};
    };

    struct GraphOutputs
    {
        RGTextureHandle display{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&               graph,
                                                            PostProcessingStage*       stage,
                                                            const FeatureGraphContext& ctx,
                                                            const Facts&               facts,
                                                            const Resolved&            resolved,
                                                            const Prepared&            prepared,
                                                            const Inputs&              inputs) const;
};

} // namespace ya
