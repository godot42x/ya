#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"

#include <optional>

namespace ya
{

struct PostProcessingStage;
class RenderSubmission;

/// Bloom as a multi-pass color contribution: extract → blur chain → composite,
/// all continuing the same color stream. The feature declares the three View
/// targets it needs and owns the pass sequence through its device helper.
///
/// Disabled: the color passes through unchanged and the feature allocates no
/// targets and no descriptor sets.
struct BloomFeature
{
    using Stage = PostProcessingStage;

    struct Settings
    {
        bool bEnabled = false;
    };

    struct Facts
    {
        Extent2D viewExtent{};
    };

    struct Inputs
    {
        RGTextureHandle color{};
    };

    struct Resolved
    {
        bool                                bEnabled = false;
        std::optional<ViewTargetRequirement> extract{};
        std::optional<ViewTargetRequirement> blur{};
        std::optional<ViewTargetRequirement> composite{};
    };

    struct PrepareContext
    {
        RenderSubmission*    submission = nullptr;
        PostProcessingStage* stage      = nullptr;
    };

    struct Prepared
    {
        BloomPassBindings bloom{};
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&               graph,
                                                            Stage*                     stage,
                                                            const FeatureGraphContext& ctx,
                                                            const Facts&               facts,
                                                            const Resolved&            resolved,
                                                            const Prepared&            prepared,
                                                            const Inputs&              inputs) const;
};

static_assert(Feature<BloomFeature>, "BloomFeature must satisfy the composition Feature protocol");

} // namespace ya
