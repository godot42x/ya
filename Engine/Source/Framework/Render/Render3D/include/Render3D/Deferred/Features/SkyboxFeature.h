#pragma once

#include "Core/Result.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Deferred/SkyboxStage.h"

namespace ya
{

struct RenderFrameData;
class RenderSubmission;

/// Skybox background feature: continues the lit color with the Scene's skybox,
/// reading the scene depth so geometry occludes it.
///
/// The frame UBO payload is built in resolve, uploaded with this feature's
/// prepared binding, and imported into the graph by append; the packet (cubemap
/// set, mesh, availability) comes from Facts.
///
/// The feature owns its device object (`SkyboxStage`), its settings and its
/// lifecycle; the pipeline drives them through this feature (D5).
struct SkyboxFeature
{
    using Stage = SkyboxStage;

    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D                                  viewExtent{};
        const RenderFrameData*                    frameData = nullptr;
        ViewOverlayFrameInputs::SkyboxInput       skybox{};
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
    };

    struct Resolved
    {
        bool bEnabled = false;
        /// The View's skybox frame UBO payload, built from Facts::frameData.
        SkyboxStage::FrameUBO frameData{};
    };

    struct PrepareContext
    {
        RenderSubmission* submission = nullptr;
        IRender*          render     = nullptr;
        SkyboxStage*      stage      = nullptr;
        uint32_t          alignment  = 1;
    };

    struct Prepared
    {
        bool                     bEnabled = false;
        /// This View's skybox frame UBO: upload slice + its own descriptor set.
        UniformBufferPassBinding frame;
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
    };

    /// The device object this feature owns (D5); the pipeline drives it.
    stdptr<SkyboxStage> _stage;

    void init(IRender* render);
    void shutdown();
    void beginFrame();
    void refreshDeviceFormats(const DeferredAttachmentFormats& formats);

    [[nodiscard]] SkyboxStage* stage() const { return _stage.get(); }

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    /// Skybox color continuation. The frame UBO is the prepared binding; the
    /// packet (cubemap set, mesh) comes from Facts.
    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&                graph,
                                                           Stage*                      stage,
                                                           const FeatureGraphContext&  ctx,
                                                           const Facts&                facts,
                                                           const Resolved&             resolved,
                                                           const Prepared&             prepared,
                                                           const Inputs&               inputs) const;
    [[nodiscard]] static constexpr std::string_view name() { return "SkyboxFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out) { (void)resolved; }
};

static_assert(Feature<SkyboxFeature>, "SkyboxFeature must satisfy the composition Feature protocol");

} // namespace ya
