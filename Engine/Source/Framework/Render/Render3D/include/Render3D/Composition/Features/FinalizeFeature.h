#pragma once

#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"

#include <string_view>
#include "Render3D/Composition/FeatureProtocol.h"

namespace ya
{

struct FrameContext;
class RenderSubmission;

/// Finalize: the mandatory display pass. It applies the configured grading (or
/// none) and the display encoding to the linear color stream and writes the
/// View's DisplayColor target, which is what publication exposes as `display`.
///
/// The pass runs every frame; grading/Bloom settings only change its look, never
/// whether it exists.
///
/// The feature owns its device object (`BasicPostprocessing`) and the grading
/// half of the postprocess settings, including the master grading switch; the
/// bloom feature owns the other half (D5).
struct FinalizeFeature
{
    using Stage = BasicPostprocessing;

    struct Settings
    {
        bool           bEnabled      = true;
        bool           bOutputIsSRGB = false;
        EFormat::T     displayFormat = EFormat::R8G8B8A8_UNORM;
        EImageUsage::T displayUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc;
        /// Diagnostic import label of this View's display target. Stated by the
        /// recipe that owns the map so each pipeline keeps its own vocabulary.
        std::string_view importLabel{};
        /// The grading state the pass runs with, and the master switch. Config
        /// loads and settings requests write the pipeline's snapshot; the recipe
        /// hands the resolved value to append, so the pass never reads a
        /// feature-held copy that a stack temporary would not have.
        PostProcessingState grading = PostProcessingState{}.gradingOnly();
        bool                bGradingEnabled = true;
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
        RenderSubmission*   submission = nullptr;
        BasicPostprocessing* stage      = nullptr;
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
        std::string_view    importLabel{};
        /// The grading the pass applies, already folded through the master
        /// switch: append records what resolve decided and nothing re-reads it.
        PostProcessingState grading{};
    };

    struct GraphOutputs
    {
        RGTextureHandle display{};
    };

    /// The device object this feature owns (D5); the pipeline drives it.
    stdptr<BasicPostprocessing> _stage;

    void init(IRender* render, EFormat::T colorFormat);
    void shutdown();
    void beginFrame();
    [[nodiscard]] BasicPostprocessing* stage() const { return _stage.get(); }

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
    [[nodiscard]] static constexpr std::string_view name() { return "FinalizeFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out)
    {
        // Same gate as append: a disabled feature publishes nothing, so its
        // target is not requested either.
        if (!resolved.bEnabled) {
            return;
        }
        out.push_back(resolved.target.toDesc());
    }
};

static_assert(Feature<FinalizeFeature>, "FinalizeFeature must satisfy the composition Feature protocol");

} // namespace ya
