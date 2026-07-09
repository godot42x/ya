#pragma once

#include "Core/Base.h"
#include "ForwardViewportStage.h"
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

struct SceneManager;
struct Texture;
struct Sampler;

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
    };

    Deleter _deleter;

    IRender* _render = nullptr;

    stdptr<IDescriptorPool> _descriptorPool = nullptr;

    std::shared_ptr<IRenderTarget> viewportRT = nullptr;

    // Shadow resources (owned here, shared to stages)
    stdptr<IDescriptorSetLayout> depthBufferDSL      = nullptr;
    DescriptorSetHandle          depthBufferShadowDS = nullptr;
    ShadowMapResources           _shadowResources;

    // ── Render stages ─────────────────────────────────────────────
    stdptr<ShadowStage>          _shadowStage;
    stdptr<ForwardViewportStage> _viewportStage;
    PostProcessingStage          _postProcessStage;

    bool     bMSAA              = false;
    Texture* viewportTexture    = nullptr;
    bool     _bViewportPassOpen = false;

    RenderingInfo _viewportRI{};
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

    void endViewportPass(ICommandBuffer* cmdBuf) override;
    bool hasOpenViewportPass() const override { return _bViewportPassOpen; }

    void                         onViewportResized(Rect2D rect) override;
    Extent2D                     getViewportExtent() const override;
    [[nodiscard]] IRenderTarget* getViewportRT() const override { return viewportRT.get(); }
    [[nodiscard]] Texture*       getViewportTexture() const override { return viewportTexture; }

    // Shadow query accessors (used by RenderRuntime for debug views)
    [[nodiscard]] bool           isShadowMappingEnabled() const override;
    [[nodiscard]] IRenderTarget* getShadowDepthRT() const override { return _shadowResources.renderTarget.get(); }
    [[nodiscard]] IImageView*    getShadowDirectionalDepthIV() const override { return _shadowResources.directionalDepthIV.get(); }
    [[nodiscard]] IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const override;
    [[nodiscard]] Texture*       getPostprocessOutputTexture() const override { return _postProcessStage.getOutputTexture(); }
    [[nodiscard]] Texture*       getBloomExtractTexture() const override { return _postProcessStage.getBloomExtractTexture(); }
    [[nodiscard]] Texture*       getBloomBlurTexture() const override { return _postProcessStage.getBloomBlurTexture(); }
    [[nodiscard]] Texture*       getBloomCompositeTexture() const override { return _postProcessStage.getBloomCompositeTexture(); }
    [[nodiscard]] bool           isPostprocessingEnabled() const override { return _postProcessStage.isEnabled(); }

  private:
    void               initViewportResources(const InitDesc& desc);
    void               initPostProcessResources(const InitDesc& desc);
    void               initShadowResources();
    void               initStageResources();
    [[nodiscard]] bool shouldSkipTick(const RenderPipelineFrameContext& frame) const;
    void               beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    void               refreshDirtyResources();
    void               syncShadowSettings();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    [[nodiscard]] ShadowRuntimeState buildShadowState() const;
    void               executeShadowPass(RenderStageContext& stageCtx);
    void               executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    void               rebuildShadowViews();
};

} // namespace ya
