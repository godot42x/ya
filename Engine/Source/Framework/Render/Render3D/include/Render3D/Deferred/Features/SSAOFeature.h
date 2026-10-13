#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/ViewPassResources.h"
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
/// Inputs: GBuffer albedo/normal/depth. The View's SSAO frame UBO is built in
/// resolve, uploaded with this feature's prepared binding, and imported into
/// the graph by append; nothing of it passes through the shared frame binding.
/// Outputs: OptionalAO, valid exactly when the feature is enabled.
///
/// When disabled, resolve declares no resource requirement, no pass is added and
/// Lighting consumes an invalid AO and binds neutral occlusion.
///
/// The feature owns its device object (`SSAOStage`), its settings and its
/// lifecycle: the pipeline drives init/beginFrame/refresh/shutdown through this
/// feature and no longer holds the stage or mirrors its settings (D5).
struct SSAOFeature
{
    using Stage = SSAOStage;

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

    /// Typed data flow into the pass: the imported GBuffer handles. The frame
    /// UBO is the feature's own prepared binding.
    struct Inputs
    {
        RGTextureHandle albedo{};
        RGTextureHandle normal{};
        RGTextureHandle depth{};
    };

    struct Outputs
    {
        OptionalAO ao;
    };

    struct Resolved
    {
        bool     bEnabled = false;
        Settings settings{};
        /// The frame UBO payload for this View. Built here so the upload in the
        /// View's binding step and the shader see one value.
        SSAOStage::FrameData frameData{};
        /// Present exactly when enabled: the AO target this feature needs.
        std::optional<ViewTargetRequirement> aoTarget{};
    };

    struct PrepareContext
    {
        RenderSubmission* submission = nullptr;
        IRender*          render     = nullptr;
        SSAOStage*        stage      = nullptr;
        uint32_t          alignment  = 1;
    };

    struct Prepared
    {
        bool                     bEnabled = false;
        /// This View's SSAO frame UBO: upload slice + its own descriptor set.
        UniformBufferPassBinding frame;
        DescriptorSetHandle      inputDS{};
    };

    struct GraphOutputs
    {
        OptionalAO ao;
    };

    /// The device object this feature owns. The pipeline passes `stage()` into
    /// the recipe's Stages until the concept drops its `Stage*` parameter.
    stdptr<SSAOStage> _stage;
    /// Runtime settings, held by the owner of the device object. Enabled by
    /// default (the pipeline's historical default); config load and settings
    /// requests overwrite it.
    Settings _settings{.bEnabled = true};

    [[nodiscard]] Settings& settings() { return _settings; }
    [[nodiscard]] const Settings& settings() const { return _settings; }

    /// Device lifecycle, driven by the owning pipeline at the same points it
    /// used to drive the stage directly.
    void init(IRender* render);
    void shutdown();
    void beginFrame();
    void refreshDeviceFormats();

    [[nodiscard]] SSAOStage* stage() const { return _stage.get(); }

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&                graph,
                                                            Stage*                      stage,
                                                            const FeatureGraphContext&   ctx,
                                                            const Facts&                 facts,
                                                            const Resolved&              resolved,
                                                            const Prepared&              prepared,
                                                            const Inputs&                inputs) const;
    [[nodiscard]] static constexpr std::string_view name() { return "SSAOFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out)
    {
        if (resolved.aoTarget.has_value()) {
            out.push_back(resolved.aoTarget->toDesc());
        }
    }
};

static_assert(Feature<SSAOFeature>, "SSAOFeature must satisfy the composition Feature protocol");

} // namespace ya
