#pragma once

#include "Core/Base.h"
#include "ForwardViewportStage.h"
#include "ForwardViewportResources.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "Runtime/App/Common/IRenderPipeline.h"
#include "Runtime/App/Common/PostProcessingStage.h"
#include "Runtime/App/Common/Shadow/Common/ShadowMapResources.h"
#include "Runtime/App/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Runtime/App/Common/Shadow/ShadowStage.h"


#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace ya
{

enum class EForwardPendingResourceRefresh : uint32_t
{
    None             = 0,
    ViewportResize   = 1 << 0,
    ShadowResources  = 1 << 1,
    AttachmentFormat = 1 << 2,
};

struct SceneManager;
struct Scene;
struct Texture;
struct Sampler;
class ResourceResolveSystem;

struct ForwardRenderPipeline : public IRenderPipeline
{
    static constexpr auto VIEWPORT_COLOR_FORMAT              = EFormat::R16G16B16A16_SFLOAT;
    static constexpr auto POSTPROCESS_COLOR_FORMAT           = EFormat::R8G8B8A8_UNORM;
    static constexpr auto DEPTH_FORMAT                       = EFormat::D32_SFLOAT_S8_UINT;
    static constexpr auto SHADOW_MAPPING_DEPTH_BUFFER_FORMAT = EFormat::D32_SFLOAT;

    struct InitDesc
    {
        IRender* render  = nullptr;
        int      windowW = 0;
        int      windowH = 0;
        ShadowSettings* shadowSettings = nullptr;
        std::function<uint64_t()> getFrameIndex;
        std::function<double()> getElapsedTimeSeconds;
        std::function<Scene*()> getActiveScene;
        std::function<ResourceResolveSystem*()> getResourceResolveSystem;
        std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
        std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;
    };

    Deleter _deleter;

    IRender* _render = nullptr;
    ShadowSettings* _shadowSettings = nullptr;
    std::function<uint64_t()> _getFrameIndex;
    std::function<double()> _getElapsedTimeSeconds;
    std::function<Scene*()> getActiveScene;
    std::function<ResourceResolveSystem*()> getResourceResolveSystem;
    std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
    std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;

    stdptr<IDescriptorPool> _descriptorPool = nullptr;

    // Shadow resources (owned here, shared to stages)
    stdptr<IDescriptorSetLayout> depthBufferDSL      = nullptr;
    DescriptorSetHandle          depthBufferShadowDS = nullptr;
    ShadowMapResources           _shadowResources;
    EFormat::T                   _shadowDepthFormat = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT;

    // ── Render stages ─────────────────────────────────────────────
    stdptr<ShadowStage>          _shadowStage;
    stdptr<ForwardViewportStage> _viewportStage;
    PostProcessingStage          _postProcessStage;

    bool               bMSAA                    = false;
    const RenderImage* _currentPostprocessOutput = nullptr;

    Extent2D      _pendingViewportExtent{};
    uint32_t      _pendingResourceRefreshMask = 0;
    RenderingInfo _viewportRI{};
    RenderTargetCreateInfo _viewportRTSpec{};
    RenderAttachmentFormats _viewportFormats{};
    ForwardViewportResources _viewportResources{};
    FrameContext  _lastTickCtx{};
    RenderPipelineFrameContext _lastFrameInput{};
    ShadowSettings _frameShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Off);

    void init(const InitDesc& desc);
    void tick(const RenderPipelineFrameContext& frame) override;
    void shutdown();

    void renderGUI(bool bRenderTreeNode);
    void renderSettingsGUI();
    void renderGeneralSettingsGUI() override;
    void renderShadowSettingsGUI() override;
    void renderPostProcessSettingsGUI() override;
    void renderTechnicalGUI();
    void renderPerformanceGUI() override;
    void renderStageInternalsGUI() override;
    bool setRenderTargetColorFormat(RenderTargetEditorCatalog::Entry::EOwner owner,
                                    uint32_t                                 attachmentIndex,
                                    EFormat::T                               format) override;
    bool setRenderTargetDepthFormat(RenderTargetEditorCatalog::Entry::EOwner owner,
                                    EFormat::T                               format) override;

    void                         onViewportResized(Rect2D rect) override;
    Extent2D                     getViewportExtent() const override;
    [[nodiscard]] EFormat::T     getViewportColorFormat() const override;
    [[nodiscard]] EFormat::T     getViewportDepthFormat() const override;
    [[nodiscard]] const ForwardViewportResources& getCurrentViewportResources() const { return _viewportResources; }
    void appendRenderTargetEditorEntries(RenderTargetEditorCatalog& catalog) const override;

    [[nodiscard]] bool           isShadowMappingEnabled() const override;
    [[nodiscard]] std::shared_ptr<IImage> getShadowDepthImage() const override { return _shadowResources.depthImage; }
    [[nodiscard]] RenderImage*   getViewportOutputImage() const override { return bMSAA ? _viewportResources.resolveImage : _viewportResources.colorImage; }
    [[nodiscard]] Texture*       getViewportDepthTexture() const override { return _viewportResources.depth; }
    [[nodiscard]] IImageView*    getShadowDirectionalDepthIV() const override { return _shadowResources.directionalDepthIV.get(); }
    [[nodiscard]] IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const override;
    [[nodiscard]] RenderImage*   getPostprocessOutputImage() const override { return const_cast<RenderImage*>(_currentPostprocessOutput); }
    [[nodiscard]] RenderImage*   getBloomExtractImage() const override { return _postProcessStage.getBloomExtractImage(); }
    [[nodiscard]] RenderImage*   getBloomBlurImage() const override { return _postProcessStage.getBloomBlurImage(); }
    [[nodiscard]] RenderImage*   getBloomCompositeImage() const override { return _postProcessStage.getBloomCompositeImage(); }
    [[nodiscard]] bool           isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }

  private:
    void               initViewportResources(const InitDesc& desc);
    void               initPostProcessResources(const InitDesc& desc);
    void               initShadowResources();
    void               initStageResources();
    [[nodiscard]] bool shouldSkipTick(const RenderPipelineFrameContext& frame) const;
    void               beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    void               markPendingResourceRefresh(EForwardPendingResourceRefresh refresh);
    [[nodiscard]] bool hasPendingResourceRefresh(EForwardPendingResourceRefresh refresh) const;
    void               clearPendingResourceRefresh(EForwardPendingResourceRefresh refresh);
    void               requestViewportResize(Extent2D extent);
    void               requestShadowResourceRefresh();
    void               applyPendingResourceRefreshes();
    void               syncFrameSettings(const RenderPipelineFrameContext& frame);
    void               recreateViewportResources();
    void               refreshViewportSnapshot();
    void               refreshViewportStageState();
    void               refreshShadowStageState();
    void               finalizeViewportPass(ICommandBuffer* cmdBuf);
    void               syncShadowSettings();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    [[nodiscard]] ShadowRuntimeState buildShadowState() const;
    void               executeShadowPass(RenderStageContext& stageCtx);
    void               executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    void               rebuildShadowViews();
    void               applyShadowSettings(const ShadowSettings& shadowSettings);
};

} // namespace ya
