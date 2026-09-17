#pragma once

#include "Core/Math/Geometry.h"
#include "Render3D/Deferred/DeferredGBufferResources.h"
#include "Render3D/Deferred/DeferredFrameGraphResources.h"
#include "Render3D/Deferred/DeferredFrameGraphOrchestrator.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Deferred/DeferredPipelineDebugViews.h"
#include "Render3D/Deferred/DeferredViewportResources.h"
#include "Render3D/Deferred/GBufferStage.h"
#include "Render3D/Deferred/LightStage.h"
#include "RHI/Core/DescriptorSet.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/RenderTargetCreateInfo.h"
#include "RHI/Render.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/IRenderRuntimeServices.h"
#include "Render3D/Common/EntityIdViewportPass.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/Shadow/Common/ShadowMapResources.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/Shadow/ShadowStage.h"
#include "Render3D/Services/RenderSharedResourceProvider.h"
#include "Render3D/Deferred/SSAOStage.h"
#include "Render3D/Deferred/ViewportOverlayStage.h"


#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>


namespace ya
{

struct AppAutomationShadowOverrides;
struct Sampler;
struct Mesh;
struct RenderTargetCatalog;
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
    IRenderRuntimeServices* runtimeServices = nullptr;
};

struct YA_RENDER_3D_API DeferredRenderPipeline : public IRenderPipeline
{
    friend class DeferredRenderPipelineTestAccess;

    struct SettingsSnapshot
    {
        bool           bReverseViewportY = true;
        bool           bSSAOEnabled      = true;
        float          ssaoRadius        = 0.6f;
        float          ssaoBias          = 0.025f;
        float          ssaoPower         = 1.5f;
        float          ssaoIntensity     = 2.5f;
        bool           bPBRDiffuseIBL    = true;
        bool           bPBRSpecularIBL   = true;
        ShadowSettings shadow{};
        PostProcessingState postProcessing{};
    };
    using InitDesc = DeferredRenderInitDesc;

    IRender* _render = nullptr;
    ShadowSettings* _shadowSettings = nullptr;
    const AppAutomationShadowOverrides* _automationShadowOverrides = nullptr;
    stdptr<IDescriptorSetLayout> _environmentLightingDSL = nullptr;
    IRenderRuntimeServices* _runtimeServices = nullptr;

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
    DeferredFrameGraphOrchestrator _frameGraphOrchestrator;
    stdptr<DeferredFrameResourceSet> _frameResources;
    stdptr<GBufferStage>         _gBufferStage;
    stdptr<SSAOStage>            _ssaoStage;
    stdptr<LightStage>           _lightStage;
    stdptr<ViewportOverlayStage> _overlayStage;
    PostProcessingStage          _postProcessStage;

    ShadowMapResources                                              _shadowResources;
    Mesh*                                                           _defaultSkyboxMesh = nullptr;

    bool     _bReverseViewportY    = true;
    bool     _bEnableSSAO          = true;
    float    _ssaoRadius           = 0.6f;
    float    _ssaoBias             = 0.025f;
    float    _ssaoPower            = 1.5f;
    float    _ssaoIntensity        = 2.5f;
    bool     _bEnablePBRDiffuseIBL = true;
    bool     _bEnablePBRSpecularIBL = true;

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
    DeferredPipelineDebugViews _debugViews{};

    // ── Frame state ───────────────────────────────────────────────────
    EntityIdViewportPass       _entityIdPass{};
    ShadowSettings             _frameShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Off);
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;
    RGTopologyDescription               _lastFrameGraphTopology{};

    DeferredRenderPipeline() = default;
    ~DeferredRenderPipeline();

    void init(const InitDesc& desc);
    ViewFamilyRenderResult recordFamily(const ViewFamilyRecordContext& ctx) override;
    void shutdown();

    void onViewportResized(Rect2D rect) override;

    Extent2D getViewportExtent() const override
    {
        return _viewportRTSpec.extent;
    }
    EFormat::T getViewportColorFormat() const override;
    EFormat::T getViewportDepthFormat() const override;

    IImageView* getDebugAlbedoRGBView() const { return _debugAlbedoRGBView.get(); }
    IImageView* getDebugSpecularAlphaView() const { return _debugSpecularAlphaView.get(); }
    const DeferredGBufferResources& getCurrentGBufferResources() const { return _debugViews.gBufferResources; }
    const DeferredViewportResources& getCurrentViewportResources() const { return _debugViews.viewportResources; }
    std::shared_ptr<RenderTexture> getViewportOutputImageShared() const { return _debugViews.viewportResources.colorOwner; }
    std::shared_ptr<RenderTexture> getPostprocessOutputImageShared() const { return _debugViews.postprocess; }
    std::shared_ptr<RenderTexture> getBloomExtractImageShared() const { return _debugViews.bloomExtract; }
    std::shared_ptr<RenderTexture> getBloomBlurImageShared() const { return _debugViews.bloomBlur; }
    std::shared_ptr<RenderTexture> getBloomCompositeImageShared() const { return _debugViews.bloomComposite; }
    const RGTopologyDescription& getLastFrameGraphTopology() const { return _lastFrameGraphTopology; }
    void setSSAOEnabled(bool enabled)
    {
        _bEnableSSAO = enabled;
    }
    [[nodiscard]] SettingsSnapshot buildSettingsSnapshot() const;
    /// Pending snapshot if a request is in flight, otherwise the applied snapshot.
    [[nodiscard]] SettingsSnapshot resolveSettingsSnapshot() const;
    void requestSettings(const SettingsSnapshot& settings);
    DeferredPipelineDebugViews buildDebugViews() const;
    void appendRenderTargetEntries(RenderTargetCatalog& catalog) const override;
    bool setRenderTargetDepthFormat(RenderTargetCatalog::Entry::EOwner owner, EFormat::T format) override;
    bool setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner, uint32_t attachmentIndex, EFormat::T format) override;

    std::shared_ptr<RenderTexture> getViewportDepthImageShared() const override { return _debugViews.viewportResources.depthOwner; }
    std::shared_ptr<RenderTexture> getEntityIdImageShared() const override { return _debugViews.viewportResources.entityIdOwner; }
    bool           isShadowMappingEnabled() const override;
    std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const override;
    std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const override;
    bool     isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }
    [[nodiscard]] EFormat::T getPostprocessColorFormat() const override { return POSTPROCESS_COLOR_FORMAT; }

  private:
    void               loadPersistentSettings();
    void               initRenderTargetSpecs(Extent2D extent);
    void               initPipelineState(const InitDesc& desc);
    void               initStages();
    void               resolveRuntimeFormats();
    [[nodiscard]] DeferredAttachmentFormats buildGBufferSnapshotFormats() const;
    [[nodiscard]] DeferredAttachmentFormats buildViewportSnapshotFormats() const;
    [[nodiscard]] bool shouldSkipView(const RenderPipelineFrameContext& frame) const;
    void               beginViewRecording(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH);
    void               invalidateGBufferDependentViews();
    [[nodiscard]] DeferredGBufferResources buildPublishedGBufferResources(const RenderGraphExecutionResult& result, uint64_t viewId) const;
    [[nodiscard]] DeferredViewportResources buildPublishedViewportResources(
        const RenderGraphExecutionResult& result,
        uint64_t viewId,
        const std::shared_ptr<RenderTexture>& depthOwner) const;
    [[nodiscard]] RenderViewOutput collectViewOutput(const RenderGraphExecutionResult& result,
                                                     const DeferredFrameGraphResources& graphResources,
                                                     const CameraFrameInput& camera,
                                                     const SceneViewportTask* task,
                                                     uint64_t viewId) const;
    void               refreshGBufferStageState();
    void               refreshViewportStageState();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ViewportOverlayStage::FrameInputs buildOverlayFrameInputs(
        const RenderPipelineFrameContext& frame,
        EnvironmentLightingSceneResources& environmentLighting,
        DescriptorSetHandle& environmentLightingDS) const;
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    void               syncFrameSettings(const RenderPipelineFrameContext& frame);
    void               prepareShadowPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    bool               appendDeferredViewToGraph(RenderGraph& graph,
                                                 const RenderPipelineFrameContext& frame,
                                                 RenderStageContext& stageCtx,
                                                 uint32_t vpW,
                                                 uint32_t vpH,
                                                 ViewportOverlayStage::FrameInputs& overlayInputs,
                                                 EnvironmentLightingSceneResources& environmentLighting,
                                                 DescriptorSetHandle environmentLightingDS,
                                                 FrameContext& postContext,
                                                 DeferredFrameGraphResources& graphResources,
                                                 std::optional<RGPassHandle> familyPredecessor);
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
