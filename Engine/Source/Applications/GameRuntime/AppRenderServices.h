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
    void                                                 setViewportRect(Rect2D rect);
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
    void                                                 setCameraPreviewHostOwned(bool bOwned);
    void                                                 setCameraPreviewEntityUUID(uint64_t uuid);
    [[nodiscard]] bool                                   isCameraPreviewHostOwned() const;
    [[nodiscard]] uint64_t                               getCameraPreviewEntityUUID() const;

  private:
    AppRenderState* _state = nullptr;
};

} // namespace ya
