#pragma once

#include "Core/Math/Geometry.h"
#include "DeferredGBufferResources.h"
#include "DeferredPipelineDebugViews.h"
#include "DeferredViewportResources.h"
#include "GBufferStage.h"
#include "LightStage.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderImage.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "Runtime/App/Common/IRenderPipeline.h"
#include "Runtime/App/Common/PostProcessingStage.h"
#include "Runtime/App/Common/Shadow/Common/ShadowMapResources.h"
#include "Runtime/App/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Runtime/App/Common/Shadow/ShadowStage.h"
#include "Runtime/App/RenderSharedResourceProvider.h"
#include "SSAOStage.h"
#include "ViewportOverlayStage.h"


#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>


namespace ya
{

struct AppAutomationShadowOverrides;
struct SceneManager;
struct Scene;
struct Sampler;
struct DebugRenderSystem;
struct RenderTargetEditorCatalog;
class ResourceResolveSystem;

enum class EDeferredPendingResourceRefresh : uint32_t
{
    None             = 0,
    ViewportResize   = 1 << 0,
    ShadowResources  = 1 << 1,
    SharedDepth      = 1 << 2,
    AttachmentFormat = 1 << 3,
};

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
    ShadowSettings* shadowSettings = nullptr;
    const AppAutomationShadowOverrides* automationShadowOverrides = nullptr;
    std::function<void(std::function<void()>)> queueFrameTask;
    stdptr<IDescriptorSetLayout> environmentLightingDSL = nullptr;
    std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;
    std::function<RenderSharedResourceProvider::EnvironmentLightingTextureSet(Scene*)> resolveSceneEnvironmentLightingTextures;
    std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
    std::function<DebugRenderSystem&()> getDebugRenderSystem;
    std::function<Scene*()> getActiveScene;
    std::function<ResourceResolveSystem*()> getResourceResolveSystem;
};

struct DeferredRenderPipeline : public IRenderPipeline
{
    using InitDesc = DeferredRenderInitDesc;

    IRender* _render = nullptr;
    ShadowSettings* _shadowSettings = nullptr;
    const AppAutomationShadowOverrides* _automationShadowOverrides = nullptr;
    std::function<void(std::function<void()>)> _queueFrameTask;
    stdptr<IDescriptorSetLayout> _environmentLightingDSL = nullptr;
    std::function<DescriptorSetHandle(Scene*)> _getSceneEnvironmentLightingDescriptorSet;
    std::function<RenderSharedResourceProvider::EnvironmentLightingTextureSet(Scene*)> _resolveSceneEnvironmentLightingTextures;
    std::function<DescriptorSetHandle(Scene*)> _getSceneSkyboxDescriptorSet;
    std::function<DebugRenderSystem&()> _getDebugRenderSystem;
    std::function<Scene*()> _getActiveScene;
    std::function<ResourceResolveSystem*()> _getResourceResolveSystem;

    // ── Render targets ────────────────────────────────────────────────
    stdptr<IRenderTarget> _gBufferRT;
    stdptr<IRenderTarget> _viewportRT;

    static constexpr EFormat::T LINEAR_FORMAT            = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T SIGNED_LINEAR_FORMAT     = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T VIEWPORT_COLOR_FORMAT    = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T POSTPROCESS_COLOR_FORMAT = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T SHADING_MODEL_FORMAT     = EFormat::R8_UNORM;
    static constexpr EFormat::T DEPTH_FORMAT             = EFormat::D32_SFLOAT;
    static constexpr EFormat::T SHADOW_DEPTH_FORMAT      = EFormat::D32_SFLOAT;
    EFormat::T _gBufferSignedLinearFormat                = SIGNED_LINEAR_FORMAT;
    EFormat::T _viewportColorFormat                      = VIEWPORT_COLOR_FORMAT;
    EFormat::T _sharedDepthFormat                        = DEPTH_FORMAT;

    // ── Render stages ─────────────────────────────────────────────────
    stdptr<ShadowStage>          _shadowStage;
    stdptr<GBufferStage>         _gBufferStage;
    stdptr<SSAOStage>            _ssaoStage;
    stdptr<LightStage>           _lightStage;
    stdptr<ViewportOverlayStage> _overlayStage;
    PostProcessingStage          _postProcessStage;

    ShadowMapResources                                              _shadowResources;

    Texture* viewportTexture       = nullptr;
    bool     _bReverseViewportY    = true;
    bool     _bEnableSSAO          = true;

    bool     _bShadowSettingsChangePending    = false;
    ShadowSettings _pendingShadowSettings{};

    uint32_t   _lastPointLightCount = 0;
    uint32_t   _lastDrawCount       = 0;
    EFormat::T _shadowDepthFormat   = SHADOW_DEPTH_FORMAT;

    // ── Debug views ───────────────────────────────────────────────────
    stdptr<IImageView> _debugAlbedoRGBView;
    stdptr<IImageView> _debugSpecularAlphaView;
    ImageViewHandle    _cachedAlbedoSpecImageViewHandle = nullptr;
    Extent2D           _pendingViewportExtent{};
    uint32_t           _pendingResourceRefreshMask = 0;
    const RenderImage* _currentSSAOOutput          = nullptr;
    const RenderImage* _currentPostprocessOutput   = nullptr;

    // ── Frame state ───────────────────────────────────────────────────
    DeferredGBufferResources   _currentGBufferResources{};
    DeferredViewportResources  _currentViewportResources{};
    FrameContext               _lastTickCtx{};
    RenderPipelineFrameContext _lastFrameInput{};
    ShadowSettings             _frameShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Off);
    RenderSharedResourceProvider::EnvironmentLightingTextureSet _currentEnvironmentLightingTextures{};
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    DeferredRenderPipeline() = default;
    ~DeferredRenderPipeline();

    void init(const InitDesc& desc);
    void tick(const RenderPipelineFrameContext& frame) override;
    void shutdown();

    void renderGUI(bool bRenderTreeNode = true);
    void renderSettingsGUI();
    void renderGeneralSettingsGUI() override;
    void renderLightingSettingsGUI() override;
    void renderAOSettingsGUI() override;
    void renderPostProcessSettingsGUI() override;
    void renderShadowSettingsGUI() override;
    void renderTechnicalGUI();
    void renderPerformanceGUI() override;
    void renderStageInternalsGUI() override;

    void onViewportResized(Rect2D rect) override;

    Extent2D getViewportExtent() const override
    {
        if (_currentViewportResources.color) {
            return _currentViewportResources.color->getExtent();
        }
        if (_currentViewportResources.depth) {
            return _currentViewportResources.depth->getExtent();
        }
        return {};
    }
    EFormat::T getViewportColorFormat() const override { return _viewportColorFormat; }
    EFormat::T getViewportDepthFormat() const override { return _sharedDepthFormat; }

    IImageView* getDebugAlbedoRGBView() const { return _debugAlbedoRGBView.get(); }
    IImageView* getDebugSpecularAlphaView() const { return _debugSpecularAlphaView.get(); }
    RenderImage* getSSAOTexture() const { return const_cast<RenderImage*>(_currentSSAOOutput); }
    const DeferredGBufferResources& getCurrentGBufferResources() const { return _currentGBufferResources; }
    const DeferredViewportResources& getCurrentViewportResources() const { return _currentViewportResources; }
    DeferredPipelineDebugViews buildDebugViews() const;
    void appendRenderTargetEditorEntries(RenderTargetEditorCatalog& catalog) const override;
    void setSharedDepthFormat(EFormat::T format) override;
    bool setRenderTargetColorFormat(RenderTargetEditorCatalog::Entry::EOwner owner, uint32_t attachmentIndex, EFormat::T format) override;

    // Access GBuffer RT for debug views
    IRenderTarget* getGBufferRT() const { return _gBufferRT.get(); }
    IRenderTarget* getViewportRT() const override { return _viewportRT.get(); }
    IRenderTarget* getShadowDepthRT() const { return _shadowResources.renderTarget.get(); }
    Texture*       getShadowDepthTexture() const override { return _shadowStage ? _shadowStage->getDirectionalDepthTexture() : nullptr; }
    Texture*       getViewportDepthTexture() const override { return _currentViewportResources.depth; }
    Texture*       getViewportTexture() const override { return viewportTexture; }
    bool           isShadowMappingEnabled() const override;
    IImageView*    getShadowDirectionalDepthIV() const override { return _shadowResources.directionalDepthIV.get(); }
    IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const override
    {
        if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) {
            return nullptr;
        }
        return _shadowResources.pointFaceIVs[pointLightIndex][faceIndex].get();
    }
    RenderImage* getBloomExtractImage() const override { return _postProcessStage.getBloomExtractImage(); }
    RenderImage* getBloomBlurImage() const override { return _postProcessStage.getBloomBlurImage(); }
    RenderImage* getBloomCompositeImage() const override { return _postProcessStage.getBloomCompositeImage(); }
    bool     isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }
    RenderImage* getPostprocessOutputImage() const override { return const_cast<RenderImage*>(_currentPostprocessOutput); }

  private:
    void               loadPersistentSettings();
    void               initPipelineState(const InitDesc& desc);
    void               initStages();
    void               resolveRuntimeFormats();
    [[nodiscard]] bool shouldSkipTick(const RenderPipelineFrameContext& frame) const;
    void               beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH);
    void               validateNoPendingAttachmentRefresh() const;
    void               invalidateGBufferDependentViews();
    void               flushGBufferResources();
    void               flushViewportResources();
    void               refreshGBufferSnapshot();
    void               refreshViewportSnapshot();
    void               refreshGBufferStageState();
    void               refreshViewportStageState();
    void               refreshCurrentFrameResources();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    void               updateStageFrameInputs(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    void               syncFrameSettings(const RenderPipelineFrameContext& frame);
    void               executeShadowPass(RenderStageContext& stageCtx);
    void               handoffShadowDepthForSampling(ICommandBuffer* cmdBuf);
    void               executeDeferredMainGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH);
    void               saveShadowSettingsToConfig(const ShadowSettings& shadowSettings) const;
    [[nodiscard]] ShadowRuntimeState buildShadowState() const;
    void               markPendingResourceRefresh(EDeferredPendingResourceRefresh refresh);
    [[nodiscard]] bool hasPendingResourceRefresh(EDeferredPendingResourceRefresh refresh) const;
    void               clearPendingResourceRefresh(EDeferredPendingResourceRefresh refresh);
    void               applyPendingResourceRefreshes();
    void               requestViewportResize(Extent2D extent);
    void               requestShadowResourceRefresh();
    void               initRenderTargets(Extent2D extent);
    void               initShadowResources();
    void               destroyShadowResources();
    void               syncShadowSettings();
    void               applyShadowSettings(const ShadowSettings& shadowSettings);
    void               queueShadowSettingsChange(const ShadowSettings& shadowSettings);
};

} // namespace ya
