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
///
/// Shared by both world pipelines: each resolves it with its own import label
/// prefix so the View's diagnostics keep naming the pipeline they belong to.
struct BloomFeature
{
    using Stage = PostProcessingStage;

    struct Settings
    {
        bool bEnabled = false;
        /// Prefix of this View's diagnostic import labels ("Deferred.",
        /// "ForwardView."). Stated by the recipe that owns the map.
        std::string_view importLabelPrefix{};
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
        std::string_view                    importLabelPrefix{};
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
    [[nodiscard]] static constexpr std::string_view name() { return "BloomFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out)
    {
        if (resolved.extract.has_value()) {
            out.push_back(resolved.extract->toDesc());
        }
        if (resolved.blur.has_value()) {
            out.push_back(resolved.blur->toDesc());
        }
        if (resolved.composite.has_value()) {
            out.push_back(resolved.composite->toDesc());
        }
    }
};

static_assert(Feature<BloomFeature>, "BloomFeature must satisfy the composition Feature protocol");

} // namespace ya
