#pragma once

#include "GameRuntime/HostRenderSettings.h"
#include "GameRuntime/HostViewportView.h"
#include "Render3D/Common/RenderPipelineSettings.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/SceneViewDesc.h"
#include "RHI/Core/SurfaceId.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderSurfaceContext;
struct ShaderStorage;
struct ShadowSettings;
struct ImageResource;
struct DebugRenderSystem;
struct RenderDiagnosticsService;
struct RenderDeviceState;
struct RGTopologyDescription;
struct Scene;
struct AppRenderState;

class YA_GAME_RUNTIME_API AppRenderServices
{
  public:
    AppRenderServices() = default;
    explicit AppRenderServices(AppRenderState* state)
        : _state(state)
    {
    }

    void bind(AppRenderState* state) { _state = state; }

    [[nodiscard]] IRender*                     getRender() const;
    template <typename T>
    [[nodiscard]] T* getRender() const
    {
        return static_cast<T*>(getRender());
    }
    [[nodiscard]] std::shared_ptr<ShaderStorage>         getShaderStorage() const;
    [[nodiscard]] RenderDeviceState*                     getDeviceState() const;
    /// Renderer-derived: did the previous tick produce content for this Scene?
    /// Answers "will poses sampled here be consumed" for systems that only need
    /// to work for what gets drawn, without a switch that describes a viewport.
    [[nodiscard]] bool                                   wasSceneRenderedLastTick(const Scene* scene) const;
    void                                                 setRenderScale(float scale);
    [[nodiscard]] float                                  getRenderScale() const;
    /// The resolution the host viewport's View renders at, in pixels. A render
    /// setting, not a window measurement: the window only decides how the
    /// resulting image is presented. Seeded from the size the window was created
    /// with; changing it resizes what is rendered, not the window.
    void                                                 setRenderResolution(Extent2D resolution);
    /// The *requested* resolution. It is what the host viewport's View will be
    /// sized from, so it is the honest answer for reporting and for a
    /// read-modify-write in the control plane. The rectangle actually rendered
    /// last tick is the renderer's published output, not this.
    [[nodiscard]] Extent2D                               getRenderResolution() const;
    [[nodiscard]] ShadowSettings&                        getShadowSettings();
    [[nodiscard]] const ShadowSettings&                  getShadowSettings() const;
    [[nodiscard]] bool                                   isShadowMappingEnabled() const;
    [[nodiscard]] std::shared_ptr<ImageResource>         getShadowDirectionalDepthResource() const;
    [[nodiscard]] std::shared_ptr<ImageResource>         getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                                   isGradingEnabled() const;
    [[nodiscard]] const HostRenderSettings&              getHostRenderSettings() const;
    /// The View the host window shows for the frame just recorded, with the
    /// camera it was rendered from. `viewId == 0` means this frame showed none.
    [[nodiscard]] const HostViewportView&                getHostViewportView() const;

    // === The renderer, named on the app's terms ===
    //
    // The editor is a module on this app, so it reads the renderer through the
    // app instead of through `RenderDeviceState`. Two things make that more than
    // a rename. Render settings and strategy identity are answered by the active
    // strategy through `IRenderPipeline` (kind / settings / compiled graph), so
    // a panel states what it wants instead of downcasting into
    // `ForwardRenderPipeline` / `DeferredRenderPipeline`. And the renderer's
    // inspection queries the editor reads (the View images, the render-target
    // catalog, the debug system, RenderDoc state) are named here, which is what
    // keeps them from being the editor's own ideas about renderer internals.

    /// Whether a renderer exists yet. Sections sync on startup paths that run
    /// before the device is up, where "no renderer" is a normal answer to show
    /// as unavailable rather than an assertion failure.
    [[nodiscard]] bool                 hasRenderer() const;

    /// Active strategy identity.
    [[nodiscard]] ERenderPipelineKind  getRenderPipelineKind() const;
    /// The requested strategy when a switch is in flight, else the active one.
    [[nodiscard]] ERenderPipelineKind  getPendingRenderPipelineKind() const;
    void                               setPendingRenderPipelineKind(ERenderPipelineKind kind);
    void                               requestRenderPipelineReload();
    /// The active strategy's settings: the pending ones when a request is in
    /// flight, so a settings panel shows what it asked for rather than the state
    /// it is being changed from.
    [[nodiscard]] RenderPipelineSettings getRenderPipelineSettings() const;
    void                                 setRenderPipelineSettings(const RenderPipelineSettings& settings);
    /// The graphs this submission compiled, one per recorded family; null when
    /// nothing was recorded yet. A pointer because "no graph" is a real answer.
    [[nodiscard]] const std::vector<RGTopologyDescription>* getFrameGraphTopologies() const;

    /// The View the host window shows, as the frame that was just recorded left
    /// it: the app's arrangement (see `HostViewportBinding`), not a renderer
    /// opinion. Null when this frame showed no View, which is an answer -- the
    /// caller decides what to show instead (the editor sizes its 2D canvas from
    /// its own panel rect, the host from its window).
    [[nodiscard]] const RenderViewOutput* getHostViewportOutput() const;
    /// Which View that is, by id. Zero when this frame showed none.
    [[nodiscard]] SceneViewId          getHostViewportViewId() const;
    /// The OS window this app presents through, resolved from the binding the
    /// app wrote at init (`AppRenderState::hostSurfaceId`). The renderer keeps
    /// every window's surface in one registry and privileges none of them, so
    /// "the window this app shows its image in" is the app's answer, and
    /// everything that means "the window I show" asks here. When a frame can
    /// present more than one window this becomes the per-display-root query
    /// (plan AB4-2d) and callers stop changing one by one. Null before the
    /// device exists or when the bound surface is gone.
    [[nodiscard]] IRenderSurfaceContext* getHostSurface() const;
    /// Which surface that is, in the device's registry. Callers that hand the
    /// surface to something which files state per window (the frame plan's
    /// present target) pass the id alongside it, so the identity survives a
    /// window being closed and reopened.
    [[nodiscard]] SurfaceId             getHostSurfaceId() const;
    [[nodiscard]] EFormat::T           getViewDepthFormat() const;
    [[nodiscard]] const RenderViewOutput* getViewOutput(SceneViewId viewId) const;
    /// The images and handles the editor's viewport shows for the host
    /// viewport's View, in the form its compositor consumes.
    [[nodiscard]] RenderViewportSnapshot buildViewportSnapshot(Scene* inspectScene) const;

    [[nodiscard]] RenderTargetCatalog  buildRenderTargetCatalog() const;
    void                               requestRenderTargetFormat(const RenderTargetFormatCommand& command);
    [[nodiscard]] DebugRenderSystem&   getDebugRenderSystem() const;
    [[nodiscard]] RenderDiagnosticsService& getDiagnosticsService() const;

  private:
    AppRenderState* _state = nullptr;
};

} // namespace ya
