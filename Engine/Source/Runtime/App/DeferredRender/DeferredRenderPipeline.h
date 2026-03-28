#pragma once

#include "Core/Math/Geometry.h"
#include "ECS/System/3D/SkyboxSystem.h"
#include "ECS/System/Render/SimpleMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Pipelines/PostProcessStage.h"
#include "Render/Render.h"
#include "Scene/Scene.h"

#include "DeferredRender.Unified_GBufferPass_PBR.slang.h"
#include "DeferredRender.Unified_GBufferPass_Phong.slang.h"
#include "DeferredRender.Unified_GBufferPass_Unlit.slang.h"
#include "DeferredRender.Unified_LightPass.slang.h"

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
};

struct DeferredRenderPipeline
{
    using InitDesc = DeferredRenderInitDesc;
    using TickDesc = DeferredRenderTickDesc;



    // GBuffer pass
    using PBRGBufferPushConstant = slang_types::DeferredRender::Unified_GBufferPass_PBR::PushConstants;
    using PBRGBufferFrameData    = slang_types::DeferredRender::Unified_GBufferPass_PBR::FrameData;
    using PBRParamUBO            = slang_types::DeferredRender::Unified_GBufferPass_PBR::PBRParamsData;

    using PhongGBufferPushConstant = slang_types::DeferredRender::Unified_GBufferPass_Phong::PushConstants;
    using PhongGBufferFrameData    = slang_types::DeferredRender::Unified_GBufferPass_Phong::FrameData;
    using PhongParamUBO            = slang_types::DeferredRender::Unified_GBufferPass_Phong::ParamsData;

    using UnlitGBufferPushConstant = slang_types::DeferredRender::Unified_GBufferPass_Unlit::PushConstants;
    using UnlitGBufferFrameData    = slang_types::DeferredRender::Unified_GBufferPass_Unlit::FrameData;
    using UnlitParamUBO            = slang_types::DeferredRender::Unified_GBufferPass_Unlit::ParamsData;


    // light
    using LightPassPushConstant = slang_types::DeferredRender::Unified_LightPass::PushConstants;
    using LightPassLightData    = slang_types::DeferredRender::Unified_LightPass::LightData;

    IRender* _render = nullptr;

    // ── Render targets ────────────────────────────────────────────────
    stdptr<IRenderTarget> _gBufferRT;
    stdptr<IRenderTarget> _viewportRT;

    const EFormat::T LINEAR_FORMAT        = EFormat::R8G8B8A8_UNORM;
    const EFormat::T SIGNED_LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    const EFormat::T SHADING_MODEL_FORMAT = EFormat::R8_UNORM;
    const EFormat::T DEPTH_FORMAT         = EFormat::D32_SFLOAT;

    // ── Shared descriptors + UBOs ─────────────────────────────────────
    stdptr<IDescriptorPool>      _deferredDSP;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    DescriptorSetHandle          _frameAndLightDS = nullptr;
    stdptr<IBuffer>              _frameUBO;
    stdptr<IBuffer>              _lightUBO;

    PBRGBufferFrameData _gBufferPassFrameData{};
    LightPassLightData  _lightPassLightData{};

    // ── PBR GBuffer pipeline ──────────────────────────────────────────
    stdptr<IGraphicsPipeline>                  _pbrGBufferPipeline;
    stdptr<IPipelineLayout>                    _pbrGBufferPPL;
    stdptr<IDescriptorSetLayout>               _pbrMaterialResourceDSL;
    stdptr<IDescriptorSetLayout>               _pbrParamsDSL;
    MaterialDescPool<PBRMaterial, PBRParamUBO> _pbrMatPool;

    // ── Phong GBuffer pipeline ────────────────────────────────────────
    stdptr<IGraphicsPipeline>                      _phongGBufferPipeline;
    stdptr<IPipelineLayout>                        _phongGBufferPPL;
    stdptr<IDescriptorSetLayout>                   _phongMaterialResourceDSL;
    stdptr<IDescriptorSetLayout>                   _phongParamsDSL;
    MaterialDescPool<PhongMaterial, PhongParamUBO> _phongMatPool;

    // ── Unlit GBuffer pipeline ────────────────────────────────────────
    stdptr<IGraphicsPipeline>                      _unlitGBufferPipeline;
    stdptr<IPipelineLayout>                        _unlitGBufferPPL;
    stdptr<IDescriptorSetLayout>                   _unlitMaterialResourceDSL;
    stdptr<IDescriptorSetLayout>                   _unlitParamsDSL;
    MaterialDescPool<UnlitMaterial, UnlitParamUBO> _unlitMatPool;

    // ── Fallback material (for meshes without any material component) ─
    UnlitMaterial* _fallbackMaterial = nullptr;

    // ── Light pass pipeline ───────────────────────────────────────────
    stdptr<IGraphicsPipeline>    _lightPipeline;
    stdptr<IPipelineLayout>      _lightPPL;
    stdptr<IDescriptorSetLayout> _lightGBufferDSL;
    DescriptorSetHandle          _lightTexturesDS = nullptr;

    // ── Shared vertex attributes ──────────────────────────────────────
    std::vector<VertexAttribute> _commonVertexAttributes = {
        VertexAttribute{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        VertexAttribute{.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        VertexAttribute{.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        VertexAttribute{.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    // ── Auxiliary systems ─────────────────────────────────────────────
    stdptr<SkyBoxSystem>         _skyboxSystem;
    stdptr<SimpleMaterialSystem> _simpleMaterialSystem; // Forward overlay (debug vis, direction cones)
    PostProcessStage             _postProcessStage;
    Texture*                     viewportTexture    = nullptr;
    bool                         _bReverseViewportY = true;

    // ── Debug views ───────────────────────────────────────────────────
    stdptr<IImageView> _debugAlbedoRGBView;
    stdptr<IImageView> _debugSpecularAlphaView;
    ImageViewHandle    _cachedAlbedoSpecImageViewHandle = nullptr;

    // ── Frame state ───────────────────────────────────────────────────
    RenderingInfo            _viewportRI{};
    RenderingInfo::ImageSpec _sharedDepthSpec{};
    FrameContext             _lastTickCtx{};
    TickDesc                 _lastTickDesc{};

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
    // ── Setup (DeferredRenderPipeline_Setup.cpp) ──
    void initRenderTargets(Extent2D extent);
    void initSharedResources();
    void initLightPassPipeline();
    void initDescriptorsAndUBOs();
    void shutdownAll();

    // ── Per-shading-model (each in its own .cpp) ──
    void initPBR();
    void preparePBR(Scene* scene);
    void drawPBR(ICommandBuffer* cmdBuf, Scene* scene);

    void initPhong();
    void preparePhong(Scene* scene);
    void drawPhong(ICommandBuffer* cmdBuf, Scene* scene);

    void initUnlit();
    void prepareUnlit(Scene* scene);
    void drawUnlit(ICommandBuffer* cmdBuf, Scene* scene);
    void drawFallback(ICommandBuffer* cmdBuf, Scene* scene);

    // ── Orchestration (stages in render order) ──
    void updateUBOs(const TickDesc& desc, Scene* scene);
    void executeGBufferPass(const TickDesc& desc, Scene* scene);

    // Viewport pass stages (share one beginRendering / endRendering)
    void beginViewportRendering(const TickDesc& desc);
    void executeLightPass(const TickDesc& desc);      // fullscreen quad
    void executeSkybox(const TickDesc& desc);         // depth LessEqual
    void executeForwardOverlay(const TickDesc& desc); // SimpleMaterial / debug
    // void executeTransparentPass(const TickDesc& desc); // TODO: sorted alpha blend
};

} // namespace ya
