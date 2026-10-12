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
struct EntityIdFeature
{
    struct Settings
    {
        bool bEnabled = true;
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
    };

    struct GraphOutputs
    {
        RGTextureHandle entityId{};
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&               graph,
                                                            EntityIdPass*              stage,
                                                            const FeatureGraphContext& ctx,
                                                            const Facts&               facts,
                                                            const Resolved&            resolved,
                                                            const Prepared&            prepared,
                                                            const Inputs&              inputs) const;
};

} // namespace ya
