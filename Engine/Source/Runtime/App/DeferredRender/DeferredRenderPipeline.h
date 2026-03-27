#pragma once

#include "Core/Math/Geometry.h"
#include "ECS/System/3D/SkyboxSystem.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Pipelines/PostProcessStage.h"
#include "Render/Render.h"

#include "DeferredRender.GBufferPass.slang.h"
#include "DeferredRender.LightPass.slang.h"

#include <memory>

namespace ya
{

struct IDeferredRenderPath;
struct SceneManager;

enum class EDeferredPathModel
{
    Phong = 0,
    PBR   = 1,
};

struct DeferredRenderInitDesc
{
    IRender* render  = nullptr;
    int      windowW = 0;
    int      windowH = 0;
};

struct DeferredRenderTickDesc
{
    ICommandBuffer*         cmdBuf                    = nullptr;
    SceneManager*           sceneManager              = nullptr;
    float                   dt                        = 0.0f;
    glm::mat4               view                      = glm::mat4(1.0f);
    glm::mat4               projection                = glm::mat4(1.0f);
    glm::vec3               cameraPos                 = glm::vec3(0.0f);
    Rect2D                  viewportRect              = {};
    float                   viewportFrameBufferScale  = 1.0f;
    int                     appMode                   = 0;
    std::vector<glm::vec2>* clicked                   = nullptr;
};

struct DeferredRenderPipeline
{
    using InitDesc = DeferredRenderInitDesc;
    using TickDesc = DeferredRenderTickDesc;

    using GBufferPushConstant = slang_types::DeferredRender::GBufferPass::PushConstants;
    using LightPassFrameData  = slang_types::DeferredRender::LightPass::FrameData;
    using LightPassLightData  = slang_types::DeferredRender::LightPass::LightData;
    using LightPassPushConstant = slang_types::DeferredRender::LightPass::PushConstants;

    IRender* _render = nullptr;

    stdptr<IRenderTarget> _gBufferRT;
    stdptr<IRenderTarget> _viewportRT;

    const EFormat::T COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;
    const EFormat::T DEPTH_FORMAT = EFormat::D32_SFLOAT;

    stdptr<SkyBoxSystem> _skyboxSystem;
    PostProcessStage     _postProcessStage;
    Texture*             viewportTexture = nullptr;

    bool _bReverseViewportY = true;

    stdptr<IImageView> _debugAlbedoRGBView;
    stdptr<IImageView> _debugSpecularAlphaView;
    ImageViewHandle    _cachedAlbedoSpecImageViewHandle = nullptr;

    RenderingInfo            _viewportRI{};
    RenderingInfo::ImageSpec _sharedDepthSpec{};
    FrameContext             _lastTickCtx{};
    TickDesc                 _lastTickDesc{};

    EDeferredPathModel          _pathModel        = EDeferredPathModel::Phong;
    EDeferredPathModel          _pendingPathModel = EDeferredPathModel::Phong;
    std::unique_ptr<IDeferredRenderPath> _path;

    DeferredRenderPipeline() = default;
    ~DeferredRenderPipeline();

    void init(const InitDesc& desc);
    void tick(const TickDesc& desc);
    void shutdown();
    void renderGUI();
    void endViewportPass(ICommandBuffer* cmdBuf);
    void onViewportResized(Rect2D rect);

    Extent2D getViewportExtent() const { return _viewportRT ? _viewportRT->getExtent() : Extent2D{}; }

    void        ensureDebugSwizzledViews();
    IImageView* getDebugAlbedoRGBView() const { return _debugAlbedoRGBView.get(); }
    IImageView* getDebugSpecularAlphaView() const { return _debugSpecularAlphaView.get(); }

  private:
    void initSharedResources(const InitDesc& desc);
    void shutdownSharedResources();
    void recreatePath(EDeferredPathModel model);
    void applyPendingPathSwitch();
};

} // namespace ya
