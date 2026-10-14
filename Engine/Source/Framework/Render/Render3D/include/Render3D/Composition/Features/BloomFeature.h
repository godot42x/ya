#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"

#include <optional>

namespace ya
{

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
///
/// The feature owns its device object (`BloomPostprocessing`) and the bloom
/// half of the postprocess settings; the finalize feature owns the other half
/// (D5). Each pipeline holds one instance, exactly as it held the old shared
/// stage.
struct BloomFeature
{
    using Stage = BloomPostprocessing;

    struct Settings
    {
        bool bEnabled = false;
        /// Prefix of this View's diagnostic import labels ("Deferred.",
        /// "ForwardView."). Stated by the recipe that owns the map.
        std::string_view importLabelPrefix{};
        /// The bloom half of the postprocess state: the five chain parameters.
        /// Grading's half never reaches this feature.
        PostProcessingState state = PostProcessingState{}.bloomOnly();
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
        /// The chain parameters this View runs with, copied out of Settings so
        /// append reads the resolved value and never a feature-held copy (the
        /// recipe holds the feature as a stack temporary).
        PostProcessingState                 state{};
    };

    struct PrepareContext
    {
        RenderSubmission*   submission = nullptr;
        BloomPostprocessing* stage      = nullptr;
    };

    struct Prepared
    {
        BloomPassBindings bloom{};
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
        /// The chain's diagnostic midpoints, typed at their producer: the
        /// collector reads these instead of export names, so an odd blur-pass
        /// count can never answer with a midpoint as the final blur.
        RGTextureHandle extract{};
        RGTextureHandle blur{};
        RGTextureHandle composite{};
    };

    /// The device object this feature owns (D5); the pipeline drives it.
    /// The chain parameters are NOT held here: they arrive through Settings and
    /// leave through Resolved, so the recipe's stack-temporary feature and the
    /// pipeline's member instance behave identically.
    stdptr<BloomPostprocessing> _stage;

    void init(const FeatureInitContext& ctx);
    void shutdown();
    void beginFrame();

    [[nodiscard]] BloomPostprocessing* stage() const { return _stage.get(); }

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
