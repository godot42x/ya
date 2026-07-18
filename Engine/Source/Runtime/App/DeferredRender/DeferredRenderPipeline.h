#pragma once

#include "Core/Math/Geometry.h"
#include "DeferredGBufferResources.h"
#include "DeferredPipelineDebugViews.h"
#include "DeferredViewportResources.h"
#include "GBufferStage.h"
#include "LightStage.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "Runtime/App/Common/IRenderPipeline.h"
#include "Runtime/App/Common/PostProcessingStage.h"
#include "Runtime/App/Common/PostProcessingState.h"
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
#include <optional>


namespace ya
{

struct AppAutomationShadowOverrides;
struct SceneManager;
struct Scene;
struct Sampler;
struct DebugRenderSystem;
struct Mesh;
struct RenderTargetCatalog;
class ResourceResolveSystem;
class DeferredRenderPipelineTestAccess;

enum class EDeferredPendingResourceRefresh : uint32_t
{
    None                = 0,
    ViewportResize      = 1 << 0,
    ShadowResources     = 1 << 1,
    SharedDepth         = 1 << 2,
    GBufferAttachments  = 1 << 3,
    ViewportAttachments = 1 << 4,
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
    stdptr<IDescriptorSetLayout> environmentLightingDSL = nullptr;
    std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;
    std::function<EnvironmentLightingSceneResources(Scene*)> resolveSceneEnvironmentLightingResources;
    std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
    std::function<DebugRenderSystem&()> getDebugRenderSystem;
    std::function<Scene*()> getActiveScene;
    std::function<ResourceResolveSystem*()> getResourceResolveSystem;
};

struct DeferredRenderPipeline : public IRenderPipeline
{
    friend class DeferredRenderPipelineTestAccess;

    struct SettingsSnapshot
    {
        bool           bReverseViewportY = true;
        bool           bSSAOEnabled      = true;
        ShadowSettings shadow{};
        PostProcessingState postProcessing{};
    };
    using InitDesc = DeferredRenderInitDesc;

    IRender* _render = nullptr;
    ShadowSettings* _shadowSettings = nullptr;
    const AppAutomationShadowOverrides* _automationShadowOverrides = nullptr;
    stdptr<IDescriptorSetLayout> _environmentLightingDSL = nullptr;
    std::function<DescriptorSetHandle(Scene*)> _getSceneEnvironmentLightingDescriptorSet;
    std::function<EnvironmentLightingSceneResources(Scene*)> _resolveSceneEnvironmentLightingResources;
    std::function<DescriptorSetHandle(Scene*)> _getSceneSkyboxDescriptorSet;
    std::function<DebugRenderSystem&()> _getDebugRenderSystem;
    std::function<Scene*()> _getActiveScene;
    std::function<ResourceResolveSystem*()> _getResourceResolveSystem;

    // ── Render targets ────────────────────────────────────────────────
    RenderTargetCreateInfo _gBufferRTSpec;
    RenderTargetCreateInfo _viewportRTSpec;

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
    Mesh*                                                           _defaultSkyboxMesh = nullptr;

    bool     _bReverseViewportY    = true;
    bool     _bEnableSSAO          = true;

    std::optional<SettingsSnapshot> _pendingSettings;

    uint32_t   _lastPointLightCount = 0;
    uint32_t   _lastDrawCount       = 0;
    EFormat::T _shadowDepthFormat   = SHADOW_DEPTH_FORMAT;

    // ── Debug views ───────────────────────────────────────────────────
    stdptr<IImageView> _debugAlbedoRGBView;
    stdptr<IImageView> _debugSpecularAlphaView;
    ImageViewHandle    _cachedAlbedoSpecImageViewHandle = nullptr;
    Extent2D           _pendingViewportExtent{};
    uint32_t           _pendingResourceRefreshMask = 0;
    stdptr<RenderImage> _currentSSAOOutput;
    stdptr<RenderImage> _currentPostprocessOutput;

    // ── Frame state ───────────────────────────────────────────────────
    DeferredGBufferResources   _currentGBufferResources{};
    DeferredViewportResources  _currentViewportResources{};
    FrameContext               _lastTickCtx{};
    RenderPipelineFrameContext _lastFrameInput{};
    ShadowSettings             _frameShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Off);
    EnvironmentLightingSceneResources _currentEnvironmentLightingTextures{};
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    DeferredRenderPipeline() = default;
    ~DeferredRenderPipeline();

    void init(const InitDesc& desc);
    void tick(const RenderPipelineFrameContext& frame) override;
    void shutdown();

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
    EFormat::T getViewportColorFormat() const override;
    EFormat::T getViewportDepthFormat() const override;

    IImageView* getDebugAlbedoRGBView() const { return _debugAlbedoRGBView.get(); }
    IImageView* getDebugSpecularAlphaView() const { return _debugSpecularAlphaView.get(); }
    const DeferredGBufferResources& getCurrentGBufferResources() const { return _currentGBufferResources; }
    const DeferredViewportResources& getCurrentViewportResources() const { return _currentViewportResources; }
    std::shared_ptr<RenderImage> getViewportOutputImageShared() const { return _currentViewportResources.colorOwner; }
    std::shared_ptr<RenderImage> getPostprocessOutputImageShared() const { return _currentPostprocessOutput; }
    std::shared_ptr<RenderImage> getBloomExtractImageShared() const { return _postProcessStage.getBloomExtractImageShared(); }
    std::shared_ptr<RenderImage> getBloomBlurImageShared() const { return _postProcessStage.getBloomBlurImageShared(); }
    std::shared_ptr<RenderImage> getBloomCompositeImageShared() const { return _postProcessStage.getBloomCompositeImageShared(); }
    void setSSAOEnabled(bool enabled)
    {
        _bEnableSSAO = enabled;
        if (_lightStage) {
            _lightStage->setSSAOTexture(_bEnableSSAO ? _currentSSAOOutput : std::shared_ptr<RenderImage>{});
        }
    }
    [[nodiscard]] SettingsSnapshot buildSettingsSnapshot() const;
    void requestSettings(const SettingsSnapshot& settings);
    DeferredPipelineDebugViews buildDebugViews() const;
    void appendRenderTargetEntries(RenderTargetCatalog& catalog) const override;
    bool setRenderTargetDepthFormat(RenderTargetCatalog::Entry::EOwner owner, EFormat::T format) override;
    bool setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner, uint32_t attachmentIndex, EFormat::T format) override;

    std::shared_ptr<IImage> getShadowDepthImage() const override { return _shadowResources.depthImage; }
    std::shared_ptr<RenderImage> getViewportDepthImageShared() const override { return _currentViewportResources.depthOwner; }
    bool           isShadowMappingEnabled() const override;
    IImageView*    getShadowDirectionalDepthIV() const override { return _shadowResources.directionalDepthIV.get(); }
    IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const override
    {
        if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) {
            return nullptr;
        }
        return _shadowResources.pointFaceIVs[pointLightIndex][faceIndex].get();
    }
    bool     isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }

  private:
    void               loadPersistentSettings();
    void               initRenderTargetSpecs(Extent2D extent);
    void               initPipelineState(const InitDesc& desc);
    void               initStages();
    void               resolveRuntimeFormats();
    [[nodiscard]] DeferredAttachmentFormats buildGBufferSnapshotFormats() const;
    [[nodiscard]] DeferredAttachmentFormats buildViewportSnapshotFormats() const;
    [[nodiscard]] bool shouldSkipTick(const RenderPipelineFrameContext& frame) const;
    void               beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH);
    void               invalidateGBufferDependentViews();
    void               syncGraphAttachmentSnapshots(
                           const RenderGraphResourceRegistry& registry,
                           const std::array<RGTextureHandle, 4>& gbufferColors,
                           RGTextureHandle gbufferDepth,
                           RGTextureHandle viewportColor);
    void               refreshGBufferStageState();
    void               refreshViewportStageState();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    void               updateStageFrameInputs(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    void               syncFrameSettings(const RenderPipelineFrameContext& frame);
    void               prepareShadowPass(RenderStageContext& stageCtx);
    void               executeDeferredMainGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH);
    [[nodiscard]] ShadowRuntimeState buildShadowState() const;
    void               markPendingResourceRefresh(EDeferredPendingResourceRefresh refresh);
    [[nodiscard]] bool hasPendingResourceRefresh(EDeferredPendingResourceRefresh refresh) const;
    void               clearPendingResourceRefresh(EDeferredPendingResourceRefresh refresh);
    void               applyPendingResourceRefreshes();
    void               requestViewportResize(Extent2D extent);
    void               requestShadowResourceRefresh();
    void               applyPendingSettings();
    void               setDeferredSharedDepthFormat(EFormat::T format);
    void               initShadowResources();
    void               destroyShadowResources();
    void               syncShadowSettings();
    void               applyShadowSettings(const ShadowSettings& shadowSettings);
};

} // namespace ya
