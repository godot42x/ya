#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "RHI/Core/DescriptorSet.h"

#include <cstdint>

namespace ya
{

struct GBufferStage;

/// GBuffer generation feature: renders the View's opaque geometry into the
/// deferred buffers.
///
/// The feature owns its resource requirements (the four color targets plus the
/// shared depth), the binding handles its pass consumes, and the pass itself.
/// The buffers are the View's imported targets; the frame/light UBOs and the
/// skinning SSBO arrive as typed inputs from the frame-level import step.
struct GBufferFeature
{
    using Stage = GBufferStage;

    /// Pipeline-resolved configuration. Formats and usages come from the
    /// pipeline's runtime formats, so allocation and PSO formats cannot drift
    /// apart from one hardcoded literal.
    struct Settings
    {
        bool           bEnabled      = true;
        EFormat::T     albedoFormat  = EFormat::Undefined;
        EFormat::T     normalFormat  = EFormat::Undefined;
        EFormat::T     ormFormat     = EFormat::Undefined;
        EFormat::T     shadingFormat = EFormat::Undefined;
        EFormat::T     depthFormat   = EFormat::Undefined;
        EImageUsage::T colorUsage    = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        EImageUsage::T depthUsage    = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled;
    };

    /// Immutable per-View facts.
    struct Facts
    {
        uint64_t viewId = 0;
        Extent2D viewExtent{};
    };

    /// Typed data flow into the pass: the View's imported host-written buffers.
    struct Inputs
    {
        RGBufferHandle frame{};
        RGBufferRange  frameRange{};
        RGBufferHandle light{};
        RGBufferRange  lightRange{};
        RGBufferHandle skinning{};
    };

    struct Outputs
    {
        ViewAttachmentDesc albedo;
        ViewAttachmentDesc normal;
        ViewAttachmentDesc orm;
        ViewAttachmentDesc shading;
        ViewAttachmentDesc depth;
    };

    struct Resolved
    {
        bool    bEnabled = false;
        Outputs outputs;
    };

    /// The View binding's descriptor sets this pass consumes.
    struct PrepareContext
    {
        DescriptorSetHandle frameAndLightDS{};
        DescriptorSetHandle skinningDS{};
    };

    struct Prepared
    {
        DescriptorSetHandle frameAndLightDS{};
        DescriptorSetHandle skinningDS{};
    };

    struct GraphOutputs
    {
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle orm;
        RGTextureHandle shading;
        RGTextureHandle depth;
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

static_assert(Feature<GBufferFeature>, "GBufferFeature must satisfy the composition Feature protocol");

} // namespace ya
