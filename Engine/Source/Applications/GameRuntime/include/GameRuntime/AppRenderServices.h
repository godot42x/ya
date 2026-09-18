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
    void                                                 setViewportFrameBufferScale(float scale);
    [[nodiscard]] float                                  getViewportFrameBufferScale() const;
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
