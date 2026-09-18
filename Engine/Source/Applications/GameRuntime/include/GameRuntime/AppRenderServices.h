#pragma once

#include "GameRuntime/HostViewState.h"
#include "Render3D/Common/RenderOverlay.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderPipeline;
struct ShaderStorage;
struct ShadowSettings;
struct ImageResource;
struct DebugRenderSystem;
struct RenderDeviceState;
struct RenderFrameCoordinator;
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
    [[nodiscard]] RenderFrameCoordinator*                getFrameCoordinator() const;
    /// Renderer-derived: did the previous tick produce content for this Scene?
    /// Answers "will poses sampled here be consumed" for systems that only need
    /// to work for what gets drawn, without a switch that describes a viewport.
    [[nodiscard]] bool                                   wasSceneRenderedLastTick(const Scene* scene) const;
    void                                                 setViewportFrameBufferScale(float scale);
    [[nodiscard]] float                                  getViewportFrameBufferScale() const;
    /// The host's request for its viewport geometry: seeded once from the window
    /// the surface was created with and overridden by the control plane. It is
    /// one of the two writers of `HostViewState::viewportRect`; the other is
    /// `GameRuntimeTickOrchestrator::declareViews`, which replaces it with the
    /// rect the View owning the host viewport declares. Do not use it as "the
    /// geometry in effect" - read getViewportRect() for that.
    void                                                 setViewportRect(Rect2D rect);
    /// Geometry in effect for the host viewport (the declaration when a View owns
    /// it, otherwise the request). Reporting and control-plane read-modify-write
    /// use this; a caller that wants to size a View declares that View's rect.
    [[nodiscard]] Rect2D                                 getViewportRect() const;
    [[nodiscard]] ShadowSettings&                        getShadowSettings();
    [[nodiscard]] const ShadowSettings&                  getShadowSettings() const;
    [[nodiscard]] IRenderPipeline*                       getRenderPipeline() const;
    [[nodiscard]] DebugRenderSystem&                     getDebugRenderSystem() const;
    [[nodiscard]] bool                                   isShadowMappingEnabled() const;
    [[nodiscard]] std::shared_ptr<ImageResource>         getShadowDirectionalDepthResource() const;
    [[nodiscard]] std::shared_ptr<ImageResource>         getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                                   isPostprocessingEnabled() const;
    [[nodiscard]] const HostViewState&                   getHostViewState() const;

  private:
    AppRenderState* _state = nullptr;
};

} // namespace ya
