#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Composition/FeatureProtocol.h"

#include <optional>

namespace ya
{

struct LightStage;
class RenderSubmission;

/// Deferred lighting feature: fullscreen pass that shades the View's GBuffer.
///
/// Consumes the GBuffer handles (albedo/normal/orm/shading/depth), the optional
/// AO output, the View's imported frame/light UBOs, and the View's shared
/// shadow/environment imports. Produces the SceneColor target.
struct LightingFeature
{
    struct Settings
    {
        bool           bEnabled          = true;
        EFormat::T     sceneColorFormat  = EFormat::Undefined;
        EImageUsage::T sceneColorUsage   = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    struct Facts
    {
        Extent2D viewExtent{};
    };

    /// Typed data flow into the pass. GBuffer/AO handles come from the
    /// upstream features; the imported shared inputs come from the frame import
    /// step (buffers) and the View's shared resource resolution (shadow,
    /// environment).
    struct Inputs
    {
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle orm;
        RGTextureHandle shading;
        RGTextureHandle depth;
        std::optional<RGTextureHandle> ssao{};

        RGBufferHandle frame{};
        RGBufferRange  frameRange{};
        RGBufferHandle light{};
        RGBufferRange  lightRange{};

        std::optional<RGTextureHandle> shadowDepth{};
        std::optional<RGTextureHandle> environmentCubemap{};
        std::optional<RGTextureHandle> environmentIrradiance{};
        std::optional<RGTextureHandle> environmentPrefilter{};
        std::optional<RGTextureHandle> environmentBrdfLut{};
    };

    struct OutputRequirements
    {
        EViewAttachment sceneColorRole   = EViewAttachment::SceneColor;
        EFormat::T      sceneColorFormat = EFormat::Undefined;
        EImageUsage::T  sceneColorUsage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    struct Resolved
    {
        bool              bEnabled = false;
        OutputRequirements outputs;
    };

    struct PrepareContext
    {
        RenderSubmission*        submission           = nullptr;
        LightStage*              stage                = nullptr;
        /// The View binding's frame/light set (also this pass's set 0).
        DescriptorSetHandle      frameAndLightDS{};
        /// The View's environment-lighting set (may be empty when the Scene has
        /// no resolved environment).
        DescriptorSetHandle      environmentLightingDS{};
    };

    struct Prepared
    {
        DescriptorSetHandle frameAndLightDS{};
        DescriptorSetHandle environmentLightingDS{};
        DescriptorSetHandle gBufferTextureDS{};
        DescriptorSetHandle shadowDS{};
    };

    struct GraphOutputs
    {
        RGTextureHandle sceneColor;
    };

    [[nodiscard]] static Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs);

    [[nodiscard]] static Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                        const PrepareContext& ctx);

    [[nodiscard]] static GraphOutputs append(RenderGraph&               graph,
                                             LightStage*                stage,
                                             const FeatureGraphContext& ctx,
                                             const Facts&               facts,
                                             const Resolved&            resolved,
                                             const Prepared&            prepared,
                                             const Inputs&              inputs);
};

} // namespace ya
