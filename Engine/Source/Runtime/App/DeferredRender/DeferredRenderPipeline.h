#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Pipelines/PostProcessStage.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "GBufferStage.h"
#include "LightStage.h"
#include "ViewportOverlayStage.h"

#include <memory>

namespace ya
{

struct SceneManager;
struct Scene;

// Shading Model IDs written to GBuffer RT3 (encoded as id/255.0 in R8_UNORM)
namespace EShadingModelID
{
constexpr uint32_t None  = 0; // background (discard in light pass)
constexpr uint32_t PBR   = 1;
constexpr uint32_t Phong = 2;
constexpr uint32_t Unlit = 3; // no lighting, direct albedo output
}; // namespace EShadingModelID

struct DeferredRenderInitDesc
{
    IRender* render  = nullptr;
    int      windowW = 0;
    int      windowH = 0;
};

struct DeferredRenderTickDesc
{
    ICommandBuffer*         cmdBuf                   = nullptr;
    SceneManager*           sceneManager             = nullptr;
    float                   dt                       = 0.0f;
    glm::mat4               view                     = glm::mat4(1.0f);
    glm::mat4               projection               = glm::mat4(1.0f);
    glm::vec3               cameraPos                = glm::vec3(0.0f);
    Rect2D                  viewportRect             = {};
    float                   viewportFrameBufferScale = 1.0f;
    int                     appMode                  = 0;
    std::vector<glm::vec2>* clicked                  = nullptr;
    RenderFrameData*        frameData                = nullptr;
};

struct DeferredRenderPipeline
{
    using InitDesc = DeferredRenderInitDesc;
    using TickDesc = DeferredRenderTickDesc;

    IRender* _render = nullptr;

    // ── Render targets ────────────────────────────────────────────────
    stdptr<IRenderTarget> _gBufferRT;
    stdptr<IRenderTarget> _viewportRT;

    static constexpr EFormat::T LINEAR_FORMAT        = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T SIGNED_LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T SHADING_MODEL_FORMAT = EFormat::R8_UNORM;
    static constexpr EFormat::T DEPTH_FORMAT         = EFormat::D32_SFLOAT;

    // ── Render stages ─────────────────────────────────────────────────
    stdptr<GBufferStage>          _gBufferStage;
    stdptr<LightStage>            _lightStage;
    stdptr<ViewportOverlayStage>  _overlayStage;
    PostProcessStage              _postProcessStage;

    Texture* viewportTexture    = nullptr;
    bool     _bViewportPassOpen = false;
    bool     _bReverseViewportY = true;

    // ── Debug views ───────────────────────────────────────────────────
    stdptr<IImageView> _debugAlbedoRGBView;
    stdptr<IImageView> _debugSpecularAlphaView;
    ImageViewHandle    _cachedAlbedoSpecImageViewHandle = nullptr;

    // ── Frame state ───────────────────────────────────────────────────
    RenderingInfo            _viewportRI{};
    RenderingInfo::ImageSpec _viewportDepthSpec{};
    FrameContext             _lastTickCtx{};
    TickDesc                 _lastTickDesc{};

    DeferredRenderPipeline() = default;
    ~DeferredRenderPipeline();

    void init(const InitDesc& desc);
    void tick(const TickDesc& desc);
    void shutdown();
    void renderGUI();
    void endViewportPass(ICommandBuffer* cmdBuf);
    bool hasOpenViewportPass() const { return _bViewportPassOpen; }
    void onViewportResized(Rect2D rect);

    Extent2D getViewportExtent() const { return _viewportRT ? _viewportRT->getExtent() : Extent2D{}; }

    IImageView* getDebugAlbedoRGBView() const { return _debugAlbedoRGBView.get(); }
    IImageView* getDebugSpecularAlphaView() const { return _debugSpecularAlphaView.get(); }

    // Access GBuffer RT for debug views
    IRenderTarget* getGBufferRT() const { return _gBufferRT.get(); }

  private:
    void initRenderTargets(Extent2D extent);
    void copyGBufferDepthToViewport(ICommandBuffer* cmdBuf);
    void beginViewportRendering(const TickDesc& desc);
};

} // namespace ya
