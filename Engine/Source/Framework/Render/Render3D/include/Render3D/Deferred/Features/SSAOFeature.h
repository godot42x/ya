#pragma once

#include "Core/Result.h"
#include "Core/Optional.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/SSAOStage.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"

namespace ya
{

struct RenderFrameData;
class ViewTargetStore;
class RenderSubmission;
struct RenderStageContext;

// Type tags for SSAOFeature
namespace ssao_tag
{
struct AO {};
struct FrameUBO {};
}

/// SSAO feature implementing the composition protocol.
///
/// Inputs: GBuffer albedo/normal/depth from GBufferFeature or legacy prelude.
/// Outputs: OptionalAO (valid when enabled, invalid when disabled).
///
/// When disabled, resolve returns no resource requirements and append contributes
/// no passes. Lighting consumes the optional AO and uses a neutral binding when invalid.
struct SSAOFeature
{
    /// Optional AO output: wraps RGTextureHandle with validity tracking.
    struct OptionalAO
    {
        RGTextureHandle handle;

        constexpr OptionalAO() : handle{RGTextureHandle::INVALID_INDEX, 0} {}
        explicit constexpr OptionalAO(RGTextureHandle h) : handle(h) {}

        [[nodiscard]] constexpr bool isValid() const { return handle.isValid(); }
        [[nodiscard]] constexpr RGTextureHandle get() const { return handle; }
    };
    struct Settings
    {
        bool  bEnabled = true;
        float radius   = 0.6f;
        float bias     = 0.025f;
        float power    = 1.5f;
        float intensity = 2.5f;
    };

    struct Facts
    {
        uint32_t                 viewId     = 0;
        Extent2D                 viewExtent = {};
        const RenderFrameData*   frameData  = nullptr;
        bool                     bReverseY  = true;
    };

    struct Inputs
    {
        CompositionTextureRef<GBufferAlbedo> albedo;
        CompositionTextureRef<GBufferNormal> normal;
        CompositionTextureRef<Depth>         depth;
    };

    struct Outputs
    {
        OptionalAO ao;
    };

    struct Resolved
    {
        bool     bEnabled   = false;
        Settings settings   = {};
        Facts    facts      = {};
        Inputs   inputs     = {};
        
        // Resource requirements (only when enabled)
        struct ResourceRequirement
        {
            EViewAttachment role   = EViewAttachment::SSAO;
            EFormat::T      format = EFormat::R8_UNORM;
            EImageUsage::T  usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
        };
        std::optional<ResourceRequirement> aoTarget;
    };

    struct Prepared
    {
        bool                bEnabled = false;
        SSAOStage::FrameData frameData = {};
        // Descriptor sets allocated in prepareBindings
        DescriptorSetHandle frameDS;
        DescriptorSetHandle inputDS;
        // UBO allocation info (the actual RGBufferHandle comes from graph in append)
        uint32_t            frameUBOOffset = 0;
        size_t              frameUBOSize = sizeof(SSAOStage::FrameData);
    };

    struct GraphOutputs
    {
        OptionalAO ao;
    };

    SSAOStage* stage = nullptr;

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    struct PrepareContext
    {
        ViewTargetStore*        viewTargets  = nullptr;
        RenderSubmission*       submission   = nullptr;
        uint32_t                flightIndex  = 0;
        SceneViewId             viewId       = 0;
    };

    struct PreparedInputs
    {
        RGBufferHandle  frameBuffer;  // Frame UBO buffer (uploaded by submission)
        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle depth;
    };

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved& resolved,
                                                   const PreparedInputs& inputs,
                                                   const PrepareContext& ctx);

    struct FeatureGraphContext
    {
        RenderGraph*             graph      = nullptr;
        const RenderStageContext* stageCtx = nullptr;
        ViewTargetStore*         viewTargets = nullptr;
    };

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(const Resolved& resolved,
                                              const Prepared& prepared,
                                              const PreparedInputs& inputs,
                                              const FeatureGraphContext& ctx);
};

// Note: SSAOFeature uses custom method signatures for C1; full Feature concept
// compliance will be achieved in C2 when the composition layer is complete.

} // namespace ya
