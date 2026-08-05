#pragma once

#include "Core/Base.h"
#include "ForwardViewportStage.h"
#include "ForwardViewportResources.h"
#include "Runtime/Rendering/Forward/ForwardFrameGraphOrchestrator.h"
#include "Runtime/Rendering/Forward/ForwardFrameResourceSet.h"
#include "Render/Core/Graph/RenderGraphExecutor.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/RenderTargetCreateInfo.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"
#include "Runtime/Rendering/Common/PostProcessingStage.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowMapResources.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"


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

struct ENGINE_API ForwardRenderPipeline : public IRenderPipeline
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
    ForwardFrameGraphOrchestrator _frameGraphOrchestrator{};
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;
    stdptr<ForwardFrameResourceSet> _frameResources;

    bool                    bMSAA                    = false;
    std::shared_ptr<RenderImage> _currentPostprocessOutput = nullptr;

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

    bool setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner,
                                    uint32_t                                 attachmentIndex,
                                    EFormat::T                               format) override;
    bool setRenderTargetDepthFormat(RenderTargetCatalog::Entry::EOwner owner,
                                    EFormat::T                               format) override;

    void                         onViewportResized(Rect2D rect) override;
    Extent2D                     getViewportExtent() const override;
    [[nodiscard]] EFormat::T     getViewportColorFormat() const override;
    [[nodiscard]] EFormat::T     getViewportDepthFormat() const override;
    [[nodiscard]] const ForwardViewportResources& getCurrentViewportResources() const { return _viewportResources; }
    [[nodiscard]] std::shared_ptr<RenderImage>    getViewportOutputImageShared() const
    {
        return bMSAA ? _viewportResources.resolveOwner : _viewportResources.colorOwner;
    }
    [[nodiscard]] std::shared_ptr<RenderImage> getPostprocessOutputImageShared() const
    {
        return _currentPostprocessOutput;
    }
    [[nodiscard]] std::shared_ptr<RenderImage> getBloomExtractImageShared() const
    {
        return _postProcessStage.getBloomExtractImageShared();
    }
    [[nodiscard]] std::shared_ptr<RenderImage> getBloomBlurImageShared() const
    {
        return _postProcessStage.getBloomBlurImageShared();
    }
    [[nodiscard]] std::shared_ptr<RenderImage> getBloomCompositeImageShared() const
    {
        return _postProcessStage.getBloomCompositeImageShared();
    }
    void appendRenderTargetEntries(RenderTargetCatalog& catalog) const override;

    [[nodiscard]] bool           isShadowMappingEnabled() const override;
    [[nodiscard]] std::shared_ptr<IImage> getShadowDepthImage() const override { return _shadowResources.depthImage; }
    [[nodiscard]] std::shared_ptr<RenderImage> getViewportDepthImageShared() const override { return _viewportResources.depthOwner; }
    [[nodiscard]] IImageView*    getShadowDirectionalDepthIV() const override { return _shadowResources.directionalDepthIV.get(); }
    [[nodiscard]] IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const override;
    [[nodiscard]] bool           isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }
    [[nodiscard]] ShadowSettings getCurrentShadowSettings() const;
    void                         requestShadowSettings(const ShadowSettings& shadowSettings);

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
    bool               executeViewportPassGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
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
