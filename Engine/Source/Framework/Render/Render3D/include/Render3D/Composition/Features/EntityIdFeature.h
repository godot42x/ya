#pragma once

#include "Graph/RenderGraph.h"
#include "RHI/Core/DescriptorSet.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

namespace ya
{

struct EntityIdPass;
struct RenderFrameData;
class RenderSubmission;

/// Picking id output: renders the View's geometry, skinned draws and the
/// camera-facing billboards into a dedicated R32_UINT target, depth-testing
/// against the scene depth so a readback under the cursor matches what is
/// visible. The feature declares and imports its own target.
///
/// Shared by both world pipelines: each resolves it with its own graph
/// vocabulary so the topology keeps naming the pipeline it belongs to.
struct EntityIdFeature
{
    using Stage = EntityIdPass;

    struct Settings
    {
        bool bEnabled = true;
        /// Topology vocabulary of the pipeline resolving this feature: the pass
        /// name base and the diagnostic import label. Stated by the recipe that
        /// owns the map, not defaulted inside the feature.
        std::string_view passName{};
        std::string_view importLabel{};
        /// Layout the scene depth is left in after this pass. The two pipelines
        /// end their depth differently (Forward keeps it attachable, Deferred
        /// leaves it shader-readable), and the pass must not silently normalize
        /// one into the other.
        EImageLayout::T depthFinalLayout = EImageLayout::ShaderReadOnlyOptimal;
    };

    struct Facts
    {
        Extent2D                             viewExtent{};
        const RenderFrameData*               frameData     = nullptr;
        const ViewOverlayStage::FrameInputs* overlayInputs = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle     depth{};
        DescriptorSetHandle skinningDS{};
    };

    struct PrepareContext
    {
        RenderSubmission*      submission = nullptr;
        IRender*               render     = nullptr;
        EntityIdPass*          stage      = nullptr;
        const RenderFrameData* frameData  = nullptr;
        uint32_t               alignment  = 1;
    };

    struct Prepared
    {
        /// The pass's frame UBO slice + the set the execute binds.
        EntityIdPassBindings bindings{};
    };

    struct Resolved
    {
        bool                bEnabled = false;
        ViewTargetRequirement target{};
        std::string_view    passName{};
        std::string_view    importLabel{};
        EImageLayout::T     depthFinalLayout = EImageLayout::ShaderReadOnlyOptimal;
    };

    struct GraphOutputs
    {
        RGTextureHandle entityId{};
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
    [[nodiscard]] static constexpr std::string_view name() { return "EntityIdFeature"; }
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

static_assert(Feature<EntityIdFeature>, "EntityIdFeature must satisfy the composition Feature protocol");

} // namespace ya
