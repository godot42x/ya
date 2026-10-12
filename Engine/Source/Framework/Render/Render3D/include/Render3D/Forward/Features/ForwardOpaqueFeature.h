#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Render3D/Forward/ForwardViewStage.h"

#include <string_view>
#include <vector>

namespace ya
{

/// Forward opaque slot: the View's lit geometry, drawn with every shading model
/// the Forward stage owns (PBR / Phong / Unlit / Simple), plus the direction
/// overlay and the debug draw. It clears the View's color and depth; the rest of
/// the Forward map continues what it wrote.
///
/// The pass is unconditional — a View with no opaque draw items still clears,
/// exactly as before — so this feature never resolves itself off. Its frame UBOs
/// (PBR/Phong/Unlit) are the View's shared inputs and arrive already uploaded in
/// the View binding; the pass reads them, it does not own them.
struct ForwardOpaqueFeature
{
    using Stage = ForwardViewStage;

    struct Settings
    {
        /// Graph vocabulary of the pipeline resolving this feature.
        std::string_view passName{};
        /// The View's stream formats, from the pipeline's runtime formats. The
        /// pass clears these attachments, so it declares their allocation.
        EFormat::T     colorFormat = EFormat::Undefined;
        EImageUsage::T colorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        EFormat::T     depthFormat = EFormat::Undefined;
        EImageUsage::T depthUsage  = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled;
    };

    struct Facts
    {
        Extent2D viewExtent{};
        /// The Forward stage's per-View pass context, built by the pipeline from
        /// the View's own declaration (never from ambient recording state).
        const ForwardViewStage::PassContext* passContext = nullptr;
        /// The View's extracted direction-gizmo snapshot; the pass draws only
        /// from this, never by querying ECS during execute.
        std::vector<ForwardDirectionGizmoInput> directionGizmos{};
    };

    /// Typed data flow into the pass: the View's color/depth stream it clears.
    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
    };

    struct Resolved
    {
        bool             bEnabled = false;
        std::string_view passName{};
        /// The View's stream: what this pass clears and leaves attachable.
        EFormat::T     colorFormat = EFormat::Undefined;
        EImageUsage::T colorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        EFormat::T     depthFormat = EFormat::Undefined;
        EImageUsage::T depthUsage  = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled;
    };

    /// Nothing to allocate: the frame UBOs are the View's shared inputs, written
    /// once by the View binding. Declared so the feature satisfies the same
    /// protocol as every other feature; the sets are referenced, not rebuilt.
    struct PrepareContext
    {
        const ForwardFrameResourceSet::Binding* frameBinding = nullptr;
    };

    struct Prepared
    {
        DescriptorSetHandle pbrFrameDescriptorSet   = nullptr;
        DescriptorSetHandle phongFrameDescriptorSet = nullptr;
        DescriptorSetHandle unlitFrameDescriptorSet = nullptr;
        DescriptorSetHandle skinningDescriptorSet   = nullptr;
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

static_assert(Feature<ForwardOpaqueFeature>, "ForwardOpaqueFeature must satisfy the composition Feature protocol");

} // namespace ya
