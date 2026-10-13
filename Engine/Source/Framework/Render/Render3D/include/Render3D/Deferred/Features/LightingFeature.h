#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Deferred/LightStage.h"
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
///
/// The feature owns its device object (`LightStage`) and the IBL settings the
/// pass reads; the pipeline drives init/setup/beginFrame/refresh (D5).
struct LightingFeature
{
    using Stage = LightStage;

    struct Settings
    {
        bool           bEnabled          = true;
        EFormat::T     sceneColorFormat  = EFormat::Undefined;
        EImageUsage::T sceneColorUsage   = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        /// IBL look switches the pass reads. Owned here (D5); the pipeline's
        /// settings snapshot projects them.
        bool           bPBRDiffuseIBL    = true;
        bool           bPBRSpecularIBL   = true;
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

    struct Resolved
    {
        bool                 bEnabled = false;
        ViewTargetRequirement target{};
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

    /// The device object this feature owns (D5); the pipeline drives it.
    stdptr<LightStage> _stage;
    /// The IBL switches the pass runs with; the pipeline's settings snapshot
    /// reads and writes these through this feature.
    bool bPBRDiffuseIBL  = true;
    bool bPBRSpecularIBL = true;

    void init(IRender* render, stdptr<IDescriptorSetLayout> frameAndLightDSL, stdptr<IDescriptorSetLayout> environmentLightingDSL);
    void shutdown();
    void beginFrame();
    /// The shadow side of this pass's state: which shadow technique the
    /// per-View sets were written from. Set at the safe point, never mid-record.
    void applyShadowState(const ShadowRuntimeState& shadowState);
    void setIBLSettings(bool bEnablePBRDiffuseIBL, bool bEnablePBRSpecularIBL);
    void refreshDeviceFormats(const DeferredAttachmentFormats& formats);

    [[nodiscard]] LightStage* stage() const { return _stage.get(); }
    [[nodiscard]] bool isPBRDiffuseIBLEnabled() const { return bPBRDiffuseIBL; }
    [[nodiscard]] bool isPBRSpecularIBLEnabled() const { return bPBRSpecularIBL; }

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
    [[nodiscard]] static constexpr std::string_view name() { return "LightingFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out)
    {
        if (!resolved.bEnabled) {
            return;
        }
        out.push_back(resolved.target.toDesc());
    }
};

static_assert(Feature<LightingFeature>, "LightingFeature must satisfy the composition Feature protocol");

} // namespace ya
