#pragma once

#include "Graph/RenderGraph.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Composition/FeatureProtocol.h"
#include "Render3D/Common/ViewOverlayInputs.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

namespace ya
{

struct RenderFrameData;

/// Billboard/gizmo overlay feature: continues the post-color-effect stream with
/// camera-facing billboards, direction gizmos and the debug overlay, reading the
/// scene depth so authored geometry occludes them.
///
/// The overlay packet (billboards/gizmos) is per-View recording data; it is
/// assembled by the pipeline and handed in, not read from live ECS here.
///
/// The feature owns its device object (`ViewOverlayStage`) and its lifecycle;
/// the debug render system the stage draws through is injected at init, not
/// reached for during recording (D5).
struct OverlayFeature
{
    using Stage = ViewOverlayStage;

    struct Settings
    {
        bool bEnabled = true;
    };

    struct Facts
    {
        Extent2D                      viewExtent{};
        ViewOverlayFrameInputs*       inputs = nullptr;
    };

    struct Inputs
    {
        RGTextureHandle color{};
        RGTextureHandle depth{};
    };

    struct PrepareContext
    {
        RenderSubmission*       submission = nullptr;
        IRender*                render     = nullptr;
        ViewOverlayStage*       stage      = nullptr;
        ViewOverlayFrameInputs* inputs     = nullptr;
        const RenderFrameData*  frameData  = nullptr;
        uint32_t                alignment  = 1;
    };

    struct Prepared
    {
        OverlayPassBindings bindings{};
    };

    struct Resolved
    {
        bool bEnabled = false;
    };

    struct GraphOutputs
    {
        RGTextureHandle color{};
    };

    /// The device object this feature owns (D5); the pipeline drives it.
    stdptr<ViewOverlayStage> _stage;

    void init(IRender* render, DebugRenderSystem* debugRenderSystem);
    void shutdown();
    void beginFrame();
    void refreshDeviceFormats(const DeferredAttachmentFormats& formats);

    [[nodiscard]] ViewOverlayStage* stage() const { return _stage.get(); }

    [[nodiscard]] Resolved resolve(const Settings& settings, const Facts& facts, const Inputs& inputs) const;

    [[nodiscard]] Result<Prepared, FeatureError> prepareBindings(const Resolved&       resolved,
                                                                 const PrepareContext& ctx) const;

    [[nodiscard]] Result<GraphOutputs, FeatureError> append(RenderGraph&                graph,
                                                            Stage*                      stage,
                                                            const FeatureGraphContext&  ctx,
                                                            const Facts&                facts,
                                                            const Resolved&             resolved,
                                                            const Prepared&             prepared,
                                                            const Inputs&               inputs) const;
    [[nodiscard]] static constexpr std::string_view name() { return "OverlayFeature"; }
    static void targets(const Resolved& resolved, std::vector<ViewAttachmentDesc>& out) { (void)resolved; }
};

static_assert(Feature<OverlayFeature>, "OverlayFeature must satisfy the composition Feature protocol");

} // namespace ya
