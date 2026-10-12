#pragma once

#include "Core/Optional.h"
#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/SSAOStage.h"

#include <optional>

namespace ya
{

struct RenderFrameData;
class RenderSubmission;

/// SSAO feature: derives ambient occlusion from the View's GBuffer.
///
/// Inputs: GBuffer albedo/normal/depth plus the View's imported SSAO frame UBO.
/// Outputs: OptionalAO, valid exactly when the feature is enabled.
///
/// When disabled, resolve declares no resource requirement, no pass is added and
/// Lighting consumes an invalid AO and binds neutral occlusion.
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
        bool  bEnabled  = false;
        float radius    = 0.6f;
        float bias      = 0.025f;
        float power     = 1.5f;
        float intensity = 2.5f;
    };

    struct Facts
    {
        Extent2D               viewExtent{};
        const RenderFrameData* frameData = nullptr;
        bool                   bReverseY = true;
    };

    /// Typed data flow into the pass: imported GBuffer handles and the View's
    /// imported SSAO frame UBO.
    struct Inputs
    {
        RGBufferHandle  frame{};
        RGBufferRange   frameRange{};
        RGTextureHandle albedo{};
        RGTextureHandle normal{};
        RGTextureHandle depth{};
    };

    struct Outputs
    {
        OptionalAO ao;
    };

    struct ResourceRequirement
    {
        EViewAttachment role   = EViewAttachment::SSAO;
        EFormat::T      format = SSAOStage::AO_FORMAT;
        EImageUsage::T  usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled;
    };

    struct Resolved
    {
        bool     bEnabled = false;
        Settings settings{};
        /// The frame UBO payload for this View. Built here so the upload in the
        /// View's binding step and the shader see one value.
        SSAOStage::FrameData frameData{};
        /// Present exactly when enabled: the AO target this feature needs.
        std::optional<ResourceRequirement> aoTarget{};
    };

    struct PrepareContext
    {
        RenderSubmission*   submission = nullptr;
        SSAOStage*          stage      = nullptr;
        /// The View binding's SSAO frame descriptor set (allocated with the
        /// View's frame payloads).
        DescriptorSetHandle frameDS{};
    };

    struct Prepared
    {
        bool                bEnabled = false;
        DescriptorSetHandle frameDS{};
        DescriptorSetHandle inputDS{};
    };

    struct GraphOutputs
    {
        OptionalAO ao;
    };

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&                graph,
                                                            SSAOStage*                   stage,
                                                            const FeatureGraphContext&   ctx,
                                                            const Facts&                 facts,
                                                            const Resolved&              resolved,
                                                            const Prepared&              prepared,
                                                            const Inputs&                inputs) const;
};

} // namespace ya
