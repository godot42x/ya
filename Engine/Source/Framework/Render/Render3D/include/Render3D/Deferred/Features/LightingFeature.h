#pragma once

#include "Core/Base.h"
#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "RHI/RenderDefines.h"

#include <optional>

namespace ya
{

struct LightStage;
class DeferredFrameResourceSet;
struct FeatureGraphContext;

/// Deferred lighting feature - fullscreen quad that reads GBuffer and computes lighting.
///
/// Consumes: GBuffer textures (albedo/normal/orm/shading/depth), optional SSAO
/// Produces: SceneColor (lit result)
struct YA_RENDER_3D_API LightingFeature
{
    // ── Settings (user-controlled) ──────────────────────────────────────────
    
    struct Settings
    {
        bool bEnabled = true;
    };
    
    // ── Facts (scene/view geometry) ─────────────────────────────────────────
    
    struct Facts
    {
        uint64_t viewId = 0;
        Extent2D viewExtent;
        bool bReverseY = false;
    };
    
    // ── Inputs (from other features) ────────────────────────────────────────
    
    struct Inputs
    {
        // GBuffer textures (from GBufferFeature)
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle orm;      // Occlusion/Roughness/Metallic
        RGTextureHandle shading;
        RGTextureHandle depth;
        
        // Optional SSAO (from SSAOFeature)
        std::optional<RGTextureHandle> ssao;
    };
    
    // ── Outputs (what this feature produces) ────────────────────────────────
    
    struct OutputRequirements
    {
        EViewAttachment sceneColorRole = EViewAttachment::SceneColor;
        EFormat::T sceneColorFormat = EFormat::R16G16B16A16_SFLOAT;
        EImageUsage::T sceneColorUsage = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };
    
    // ── Resolved (resource requirements) ────────────────────────────────────
    
    struct Resolved
    {
        bool bEnabled = false;
        OutputRequirements outputs;
    };
    
    // ── Prepared (bindings/uploads) ─────────────────────────────────────────
    
    struct Prepared
    {
        // GBuffer texture descriptors, shadow descriptors allocated here
        // Full implementation will populate these when integrated
    };
    
    // ── Graph outputs (RDG handles) ─────────────────────────────────────────
    
    struct GraphOutputs
    {
        RGTextureHandle sceneColor;
    };
    
    // ── Protocol implementation ─────────────────────────────────────────────
    
    [[nodiscard]] static Resolved resolve(
        const Settings& settings,
        const Facts& facts,
        const Inputs& inputs);
    
    [[nodiscard]] static Prepared prepareBindings(
        DeferredFrameResourceSet* resourceSet,
        const Facts& facts,
        const Resolved& resolved);
    
    [[nodiscard]] static GraphOutputs append(
        RenderGraph& graph,
        LightStage* stage,
        const FeatureGraphContext& ctx,
        const Facts& facts,
        const Resolved& resolved,
        const Prepared& prepared,
        const Inputs& inputs);
};

} // namespace ya
