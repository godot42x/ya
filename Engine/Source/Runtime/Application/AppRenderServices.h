#pragma once

#include "Runtime/Application/AppRenderFrameState.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"

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
struct IImageView;
struct DebugRenderSystem;
struct RenderRuntime;
struct AppRenderState;

class ENGINE_API AppRenderServices
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
    [[nodiscard]] RenderRuntime*                         getRenderRuntime() const;
    [[nodiscard]] ShadowSettings&                        getShadowSettings();
    [[nodiscard]] const ShadowSettings&                  getShadowSettings() const;
    [[nodiscard]] IRenderPipeline*                       getRenderPipeline() const;
    [[nodiscard]] DebugRenderSystem&                     getDebugRenderSystem() const;
    [[nodiscard]] bool                                   isShadowMappingEnabled() const;
    [[nodiscard]] bool                                   isMirrorRenderingEnabled() const;
    [[nodiscard]] bool                                   hasMirrorRenderResult() const;
    [[nodiscard]] IImageView*                            getShadowDirectionalDepthIV() const;
    [[nodiscard]] IImageView*                            getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const;
    [[nodiscard]] bool                                   isPostprocessingEnabled() const;
    [[nodiscard]] const AppRenderFrameState&             getRenderFrameState() const;
    void                                                 setExtensionRenderFrameState(const AppRenderFrameState& state);
    void                                                 clearExtensionRenderFrameState();

  private:
    AppRenderState* _state = nullptr;
};

} // namespace ya
